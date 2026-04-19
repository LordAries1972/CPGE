# KeyboardHandler Class - Comprehensive Usage Guide

## Table of Contents

1.  [Introduction](#introduction)
2.  [System Requirements](#system-requirements)
3.  [Basic Setup and Initialization](#basic-setup-and-initialization)
4.  [Configuration Options](#configuration-options)
5.  [Basic Key State Queries](#basic-key-state-queries)
6.  [Event Handler Registration](#event-handler-registration)
7.  [Hotkey Management](#hotkey-management)
8.  [Key Logging for AI Systems](#key-logging-for-ai-systems)
9.  [Advanced Features](#advanced-features)
10. [Threading and Performance](#threading-and-performance)
11. [Platform-Specific Considerations](#platform-specific-considerations)
12. [Error Handling and Debugging](#error-handling-and-debugging)
13. [Complete Example Applications](#complete-example-applications)
14. [Best Practices](#best-practices)
15. [Troubleshooting](#troubleshooting)

---

## Introduction

The `KeyboardHandler` class is a comprehensive, cross-platform singleton system designed for high-performance keyboard input handling in gaming applications. It provides thread-safe operations, advanced key combination detection, hotkey management, and AI system integration through detailed key logging.

### Key Features
- **Cross-platform support**: Windows, Linux, macOS, Android, iOS
- **Thread-safe operations** with minimal locking for real-time performance
- **Advanced key combination detection** with customizable limits
- **Hotkey management** with callback registration
- **Key logging system** for AI analysis
- **Lock-free key state queries** optimized for game loops
- **Comprehensive debugging support** with detailed logging

---

## System Requirements

### Supported Platforms
- **Windows**: Windows 10 and above (64-bit)
- **Linux**: X11-based distributions
- **macOS**: macOS 10.12 and above
- **Android**: Android API 21 and above
- **iOS**: iOS 10.0 and above

### Dependencies
- C++17 compatible compiler
- ThreadManager class (included in engine)
- Debug logging system (included in engine)
- Platform-specific libraries (automatically linked)
- ExceptHandler (Optional: but included in engine)

---

## Basic Setup and Initialization

### Including the Header

```cpp
#include "KeyboardHandler.h"
```

### Getting the Singleton Instance

```cpp
// Get the singleton instance
KeyboardHandler& keyboard = KeyboardHandler::GetInstance();
```

### Basic Initialization

```cpp
// Initialize with default configuration
bool success = keyboard.Initialize();
if (!success) {
    // Handle initialization failure
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize keyboard handler");
    return false;
}

// Enable the keyboard system
success = keyboard.EnableKeyboardSystem();
if (!success) {
    // Handle enable failure
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to enable keyboard system");
    return false;
}
```

### Cleanup

```cpp
// Clean up when done (typically in destructor or shutdown)
keyboard.DisableKeyboardSystem();
keyboard.Cleanup();
```

---

## Configuration Options

### KeyboardConfig Structure

```cpp
struct KeyboardConfig {
    bool enableKeyLogging;              // Enable key logging for AI system
    bool enableHotKeyBlocking;          // Block OS hotkeys during application focus
    bool enableKeyRepeat;               // Allow key repeat events
    uint32_t keyRepeatDelay;           // Delay before key repeat starts (ms)
    uint32_t keyRepeatRate;            // Key repeat rate (ms between repeats)
    bool enableMultiKeyDetection;       // Enable multi-key combination detection
    uint32_t maxCombinationKeys;       // Maximum keys in a combination
};
```

### Custom Configuration Example

```cpp
// Create custom configuration
KeyboardConfig config;
config.enableKeyLogging = true;        // Enable for AI analysis
config.enableHotKeyBlocking = true;    // Block Windows key, Alt+Tab, etc.
config.enableKeyRepeat = false;        // Disable key repeat for precise input
config.keyRepeatDelay = 250;          // 250ms delay before repeat
config.keyRepeatRate = 25;            // 25ms between repeats (40 Hz)
config.enableMultiKeyDetection = true; // Enable complex combinations
config.maxCombinationKeys = 6;        // Support up to 6-key combinations

// Initialize with custom configuration
bool success = keyboard.Initialize(config);
```

### Gaming-Optimized Configuration

```cpp
// Configuration optimized for competitive gaming
KeyboardConfig gamingConfig;
gamingConfig.enableKeyLogging = false;        // Disable for maximum performance
gamingConfig.enableHotKeyBlocking = true;     // Block distracting OS shortcuts
gamingConfig.enableKeyRepeat = false;         // Disable repeat for precise control
gamingConfig.enableMultiKeyDetection = true;  // Enable for complex shortcuts
gamingConfig.maxCombinationKeys = 4;         // Limit to 4 keys for performance

keyboard.Initialize(gamingConfig);
```

---

## Basic Key State Queries

### Single Key Queries

```cpp
// Check if a key is currently pressed
if (keyboard.IsKeyPressed(KeyCode::KEY_W)) {
    // Move player forward
    player.MoveForward();
}

// Check if a key was just pressed this frame (edge detection)
if (keyboard.IsKeyJustPressed(KeyCode::KEY_SPACE)) {
    // Player jump (only triggers once per press)
    player.Jump();
}

// Check if a key was just released this frame
if (keyboard.IsKeyJustReleased(KeyCode::KEY_SHIFT_LEFT)) {
    // Stop running
    player.StopRunning();
}
```

### Key Hold Duration

```cpp
// Get how long a key has been held
uint64_t holdTime = keyboard.GetKeyHoldDuration(KeyCode::KEY_E);
if (holdTime > 1000) {  // Held for more than 1 second
    // Perform long-press action
    player.InteractLong();
} else if (keyboard.IsKeyJustReleased(KeyCode::KEY_E) && holdTime > 0) {
    // Perform short-press action
    player.InteractShort();
}
```

### Multiple Key Combinations

```cpp
// Check if multiple keys are pressed simultaneously
std::vector<KeyCode> comboKeys = {KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_S};
if (keyboard.AreKeysPressed(comboKeys)) {
    // Save game (Ctrl+S)
    game.SaveGame();
}

// Complex movement combination
std::vector<KeyCode> strafeCombo = {KeyCode::KEY_W, KeyCode::KEY_A, KeyCode::KEY_SHIFT_LEFT};
if (keyboard.AreKeysPressed(strafeCombo)) {
    // Fast forward-left strafe
    player.FastStrafeLeft();
}
```

### Game Loop Integration

```cpp
void GameLoop() {
    while (gameRunning) {
        // Update key states for edge detection (call once per frame)
        keyboard.UpdateKeyStates();
        
        // Process player input
        ProcessPlayerInput();
        
        // Update game logic
        UpdateGame();
        
        // Render frame
        RenderFrame();
    }
}

void ProcessPlayerInput() {
    // Movement
    if (keyboard.IsKeyPressed(KeyCode::KEY_W)) player.MoveForward();
    if (keyboard.IsKeyPressed(KeyCode::KEY_S)) player.MoveBackward();
    if (keyboard.IsKeyPressed(KeyCode::KEY_A)) player.MoveLeft();
    if (keyboard.IsKeyPressed(KeyCode::KEY_D)) player.MoveRight();
    
    // Actions (edge-triggered)
    if (keyboard.IsKeyJustPressed(KeyCode::KEY_SPACE)) player.Jump();
    if (keyboard.IsKeyJustPressed(KeyCode::KEY_E)) player.Interact();
    
    // Shooting with modifier
    if (keyboard.IsKeyPressed(KeyCode::KEY_SHIFT_LEFT) && 
        keyboard.IsKeyJustPressed(KeyCode::KEY_SPACE)) {
        player.SpecialAttack();
    }
}
```

---

## Event Handler Registration

### Key Down Handler

```cpp
// Register custom key down handler
keyboard.SetKeyDownHandler([](KeyCode keyCode, uint32_t modifierFlags) {
    // Convert keyCode to string for logging
    std::string keyName = KeyboardHandler::GetInstance().KeyCodeToString(keyCode);
    
    // Check modifier flags
    bool ctrlPressed = (modifierFlags & 0x01) != 0;
    bool shiftPressed = (modifierFlags & 0x02) != 0;
    bool altPressed = (modifierFlags & 0x04) != 0;
    
    // Log key press with modifiers
    if (ctrlPressed) {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Ctrl+%S pressed", keyName.c_str());
    } else {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Key %S pressed", keyName.c_str());
    }
    
    // Handle specific keys
    switch (keyCode) {
        case KeyCode::KEY_ESCAPE:
            if (!ctrlPressed && !shiftPressed && !altPressed) {
                // Pure Escape key - open pause menu
                game.OpenPauseMenu();
            }
            break;
            
        case KeyCode::KEY_F1:
            // Toggle help
            game.ToggleHelp();
            break;
            
        case KeyCode::KEY_F12:
            // Take screenshot
            game.TakeScreenshot();
            break;
    }
});
```

### Key Up Handler

```cpp
// Register key up handler for release events
keyboard.SetKeyUpHandler([](KeyCode keyCode, uint32_t modifierFlags) {
    switch (keyCode) {
        case KeyCode::KEY_SHIFT_LEFT:
        case KeyCode::KEY_SHIFT_RIGHT:
            // Stop running when shift is released
            player.StopRunning();
            break;
            
        case KeyCode::KEY_TAB:
            // Close inventory/map when tab is released
            if (game.IsInventoryOpen()) {
                game.CloseInventory();
            }
            break;
    }
});
```

### Key Combination Handler

```cpp
// Register handler for key combinations
keyboard.SetKeyComboHandler([](const std::vector<KeyCode>& keys, uint32_t modifierFlags) {
    // Only process combinations with 2 or more keys
    if (keys.size() < 2) return;
    
    // Convert key combination to string for debugging
    std::string combo;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i > 0) combo += "+";
        combo += KeyboardHandler::GetInstance().KeyCodeToString(keys[i]);
    }
    
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Key combination: %S", combo.c_str());
    
    // Handle specific combinations
    if (keys.size() == 2) {
        // Two-key combinations
        auto hasKey = [&keys](KeyCode key) {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        };
        
        if (hasKey(KeyCode::KEY_ALT_LEFT) && hasKey(KeyCode::KEY_F4)) {
            // Alt+F4 - quit game
            game.RequestShutdown();
        }
        else if (hasKey(KeyCode::KEY_CTRL_LEFT) && hasKey(KeyCode::KEY_Q)) {
            // Ctrl+Q - quick save
            game.QuickSave();
        }
    }
});
```

---

## Hotkey Management

### Registering Hotkeys

```cpp
// Register single hotkey
std::vector<KeyCode> saveHotkey = {KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_S};
keyboard.RegisterHotkey(saveHotkey, []() {
    game.SaveGame();
    ui.ShowMessage("Game Saved!");
});

// Register complex hotkey combination
std::vector<KeyCode> debugHotkey = {
    KeyCode::KEY_CTRL_LEFT, 
    KeyCode::KEY_SHIFT_LEFT, 
    KeyCode::KEY_D
};
keyboard.RegisterHotkey(debugHotkey, []() {
    debug.ToggleDebugDisplay();
});
```

### Game-Specific Hotkey Setup

```cpp
void SetupGameHotkeys() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Quick save (Ctrl+S)
    kb.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_S}, []() {
        game.QuickSave();
        ui.ShowNotification("Quick Save");
    });
    
    // Quick load (Ctrl+L)
    kb.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_L}, []() {
        game.QuickLoad();
        ui.ShowNotification("Quick Load");
    });
    
    // Toggle inventory (I)
    kb.RegisterHotkey({KeyCode::KEY_I}, []() {
        game.ToggleInventory();
    });
    
    // Open map (M)
    kb.RegisterHotkey({KeyCode::KEY_M}, []() {
        game.ToggleMap();
    });
    
    // Console (~ or F1)
    kb.RegisterHotkey({KeyCode::KEY_GRAVE}, []() {
        game.ToggleConsole();
    });
    
    kb.RegisterHotkey({KeyCode::KEY_F1}, []() {
        game.ToggleConsole();
    });
    
    // Screenshot (F12)
    kb.RegisterHotkey({KeyCode::KEY_F12}, []() {
        game.TakeScreenshot();
    });
    
    // Emergency pause (Pause key)
    kb.RegisterHotkey({KeyCode::KEY_PAUSE}, []() {
        game.EmergencyPause();
    });
}
```

### Unregistering Hotkeys

```cpp
// Unregister hotkey when no longer needed
std::vector<KeyCode> oldHotkey = {KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_S};
bool success = keyboard.UnregisterHotkey(oldHotkey);
if (!success) {
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to unregister hotkey");
}
```

### Dynamic Hotkey Management

```cpp
class HotkeyManager {
private:
    KeyboardHandler& keyboard;
    std::vector<std::vector<KeyCode>> registeredHotkeys;
    
public:
    HotkeyManager() : keyboard(KeyboardHandler::GetInstance()) {}
    
    void RegisterGameHotkey(const std::vector<KeyCode>& keys, std::function<void()> callback) {
        if (keyboard.RegisterHotkey(keys, callback)) {
            registeredHotkeys.push_back(keys);
        }
    }
    
    void ClearAllHotkeys() {
        for (const auto& hotkey : registeredHotkeys) {
            keyboard.UnregisterHotkey(hotkey);
        }
        registeredHotkeys.clear();
    }
    
    ~HotkeyManager() {
        ClearAllHotkeys();
    }
};
```

---

## Key Logging for AI Systems

### Basic Key Logging

```cpp
// Get recent key log entries
std::vector<KeyLogEntry> recentKeys = keyboard.GetRecentKeyLog(20); // Last 20 entries

for (const auto& entry : recentKeys) {
    std::string keyName = keyboard.KeyCodeToString(entry.keyCode);
    std::string action = entry.isKeyDown ? "pressed" : "released";
    
    // Calculate time since event
    auto now = std::chrono::steady_clock::now();
    auto timeSince = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - entry.timestamp).count();
    
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Key %S %S %lld ms ago (modifiers: 0x%08X)",
        keyName.c_str(), action.c_str(), timeSince, entry.modifierFlags);
}
```

### AI Integration Example

```cpp
class PlayerInputAnalyzer {
private:
    KeyboardHandler& keyboard;
    std::chrono::steady_clock::time_point lastAnalysis;
    
public:
    PlayerInputAnalyzer() : keyboard(KeyboardHandler::GetInstance()) {
        lastAnalysis = std::chrono::steady_clock::now();
    }
    
    void AnalyzePlayerBehavior() {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastAnalysis = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastAnalysis);
        
        // Analyze every 5 seconds
        if (timeSinceLastAnalysis.count() >= 5) {
            std::vector<KeyLogEntry> recentKeys = keyboard.GetRecentKeyLog(50);
            
            // Analyze key patterns
            AnalyzeMovementPatterns(recentKeys);
            AnalyzeReactionTimes(recentKeys);
            AnalyzeSkillLevel(recentKeys);
            
            lastAnalysis = now;
        }
    }
    
private:
    void AnalyzeMovementPatterns(const std::vector<KeyLogEntry>& keys) {
        int wasdCount = 0;
        int arrowCount = 0;
        
        for (const auto& entry : keys) {
            if (entry.isKeyDown) {
                switch (entry.keyCode) {
                    case KeyCode::KEY_W:
                    case KeyCode::KEY_A:
                    case KeyCode::KEY_S:
                    case KeyCode::KEY_D:
                        wasdCount++;
                        break;
                    case KeyCode::KEY_ARROW_UP:
                    case KeyCode::KEY_ARROW_DOWN:
                    case KeyCode::KEY_ARROW_LEFT:
                    case KeyCode::KEY_ARROW_RIGHT:
                        arrowCount++;
                        break;
                }
            }
        }
        
        if (wasdCount > arrowCount) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Player prefers WASD movement");
        } else if (arrowCount > wasdCount) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Player prefers arrow key movement");
        }
    }
    
    void AnalyzeReactionTimes(const std::vector<KeyLogEntry>& keys) {
        // Analyze time between related key presses
        // This could be used to adjust game difficulty
        
        std::vector<uint64_t> reactionTimes;
        
        for (size_t i = 1; i < keys.size(); ++i) {
            auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
                keys[i].timestamp - keys[i-1].timestamp).count();
            
            if (timeDiff > 0 && timeDiff < 1000) { // Within 1 second
                reactionTimes.push_back(timeDiff);
            }
        }
        
        if (!reactionTimes.empty()) {
            uint64_t avgReactionTime = 0;
            for (uint64_t time : reactionTimes) {
                avgReactionTime += time;
            }
            avgReactionTime /= reactionTimes.size();
            
            debug.logDebugMessage(LogLevel::LOG_INFO, 
                L"Average reaction time: %llu ms", avgReactionTime);
        }
    }
    
    void AnalyzeSkillLevel(const std::vector<KeyLogEntry>& keys) {
        // Count key combinations and complex inputs
        int complexCombinations = 0;
        
        // This is a simplified analysis - real implementation would be more sophisticated
        for (const auto& entry : keys) {
            if (entry.modifierFlags != 0) {
                complexCombinations++;
            }
        }
        
        float skillRatio = static_cast<float>(complexCombinations) / keys.size();
        
        if (skillRatio > 0.3f) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Player appears to be experienced (high combo usage)");
        } else if (skillRatio > 0.1f) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Player appears to be intermediate");
        } else {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Player appears to be beginner (basic inputs)");
        }
    }
};
```

### Key Log Statistics

```cpp
void DisplayKeyboardStats() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Get basic statistics
    uint64_t totalKeysLogged = kb.GetTotalKeysLogged();
    
    // Get processing statistics
    uint64_t eventsProcessed;
    float avgProcessingTime;
    kb.GetKeyboardStats(eventsProcessed, avgProcessingTime);
    
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Keyboard Stats - Total keys logged: %llu, Events processed: %llu, Avg processing time: %.3f μs",
        totalKeysLogged, eventsProcessed, avgProcessingTime);
}
```

---

## Advanced Features

### Lock Key State Monitoring

```cpp
void CheckLockKeys() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    if (kb.IsCapsLockOn()) {
        ui.ShowWarning("Caps Lock is ON");
    }
    
    if (!kb.IsNumLockOn()) {
        ui.ShowWarning("Num Lock is OFF - numpad may not work as expected");
    }
    
    // Automatically fix common issues
    if (kb.IsCapsLockOn() && game.IsTypingMode()) {
        ui.ShowMessage("Caps Lock detected - consider turning it off for normal typing");
    }
}
```

### Complex Input Sequences

```cpp
class ComboSystem {
private:
    std::vector<KeyCode> currentSequence;
    std::chrono::steady_clock::time_point lastKeyTime;
    static constexpr uint64_t COMBO_TIMEOUT_MS = 1000; // 1 second timeout
    
public:
    void OnKeyPressed(KeyCode key) {
        auto now = std::chrono::steady_clock::now();
        
        // Reset sequence if too much time has passed
        if (!currentSequence.empty()) {
            auto timeSinceLastKey = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastKeyTime).count();
            
            if (timeSinceLastKey > COMBO_TIMEOUT_MS) {
                currentSequence.clear();
            }
        }
        
        // Add key to sequence
        currentSequence.push_back(key);
        lastKeyTime = now;
        
        // Check for known combos
        CheckCombos();
        
        // Limit sequence length
        if (currentSequence.size() > 10) {
            currentSequence.erase(currentSequence.begin());
        }
    }
    
private:
    void CheckCombos() {
        // Konami Code: ↑↑↓↓←→←→BA
        std::vector<KeyCode> konamiCode = {
            KeyCode::KEY_ARROW_UP, KeyCode::KEY_ARROW_UP,
            KeyCode::KEY_ARROW_DOWN, KeyCode::KEY_ARROW_DOWN,
            KeyCode::KEY_ARROW_LEFT, KeyCode::KEY_ARROW_RIGHT,
            KeyCode::KEY_ARROW_LEFT, KeyCode::KEY_ARROW_RIGHT,
            KeyCode::KEY_B, KeyCode::KEY_A
        };
        
        if (SequenceMatches(konamiCode)) {
            game.ActivateCheatMode();
            currentSequence.clear();
        }
        
        // Custom game combo: WASD in circle
        std::vector<KeyCode> circleCombo = {
            KeyCode::KEY_W, KeyCode::KEY_D, KeyCode::KEY_S, KeyCode::KEY_A
        };
        
        if (SequenceMatches(circleCombo)) {
            player.PerformSpinAttack();
            currentSequence.clear();
        }
    }
    
    bool SequenceMatches(const std::vector<KeyCode>& pattern) {
        if (currentSequence.size() < pattern.size()) {
            return false;
        }
        
        // Check if the end of current sequence matches the pattern
        auto startIt = currentSequence.end() - pattern.size();
        return std::equal(pattern.begin(), pattern.end(), startIt);
    }
};
```

### Modifier Key Utilities

```cpp
class ModifierHelper {
public:
    static bool IsCtrlPressed(uint32_t modifierFlags) {
        return (modifierFlags & 0x01) != 0;
    }
    
    static bool IsShiftPressed(uint32_t modifierFlags) {
        return (modifierFlags & 0x02) != 0;
    }
    
    static bool IsAltPressed(uint32_t modifierFlags) {
        return (modifierFlags & 0x04) != 0;
    }
    
    static bool IsWinPressed(uint32_t modifierFlags) {
        return (modifierFlags & 0x08) != 0;
    }
    
    static std::string ModifiersToString(uint32_t modifierFlags) {
        std::string result;
        
        if (IsCtrlPressed(modifierFlags)) {
            if (!result.empty()) result += "+";
            result += "Ctrl";
        }
        
        if (IsShiftPressed(modifierFlags)) {
            if (!result.empty()) result += "+";
            result += "Shift";
        }
        
        if (IsAltPressed(modifierFlags)) {
            if (!result.empty()) result += "+";
            result += "Alt";
        }
        
        if (IsWinPressed(modifierFlags)) {
            if (!result.empty()) result += "+";
            result += "Win";
        }
        
        return result;
    }
};
```

---

## Threading and Performance

### Thread Management

```cpp
// Start keyboard thread for background processing
if (!keyboard.StartKeyboardThread()) {
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to start keyboard thread");
    return false;
}

// Check thread status
if (keyboard.IsKeyboardThreadRunning()) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard thread is running");
}

// Stop thread gracefully
keyboard.StopKeyboardThread();

// Emergency termination (use sparingly)
if (emergencyShutdown) {
    keyboard.TerminateKeyboardThread();
}
```

### Performance Monitoring

```cpp
void MonitorKeyboardPerformance() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Get thread performance metrics
    float cpuUsage;
    uint64_t memoryUsage;
    
    if (kb.GetThreadPerformanceMetrics(cpuUsage, memoryUsage)) {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Keyboard thread - CPU: %.1f%%, Memory: %llu bytes",
            cpuUsage * 100.0f, memoryUsage);
        
        // Warn if performance is poor
        if (cpuUsage > 0.1f) { // More than 10% CPU
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"Keyboard thread using high CPU: %.1f%%", cpuUsage * 100.0f);
        }
        
        if (memoryUsage > 1024 * 1024) { // More than 1MB
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"Keyboard thread using high memory: %llu bytes", memoryUsage);
        }
    }
    
    // Get processing statistics
    uint64_t eventsProcessed;
    float avgProcessingTime;
    kb.GetKeyboardStats(eventsProcessed, avgProcessingTime);
    
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Events processed: %llu, Avg time: %.3f μs", eventsProcessed, avgProcessingTime);
}
```

### Performance Optimization Tips

```cpp
// Optimize for high-frequency polling
void OptimizeForPerformance() {
    KeyboardConfig config;
    
    // Disable features that aren't needed for maximum performance
    config.enableKeyLogging = false;       // Disable if AI analysis not needed
    config.enableHotKeyBlocking = true;    // Keep for better user experience
    config.enableKeyRepeat = false;        // Disable for precise input
    config.maxCombinationKeys = 3;         // Reduce if complex combos not needed
    
    KeyboardHandler::GetInstance().Initialize(config);
}

// Batch key state queries for efficiency
void ProcessInputEfficiently() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Update once per frame
    kb.UpdateKeyStates();
    
    // Query multiple keys efficiently
    bool w = kb.IsKeyPressed(KeyCode::KEY_W);
    bool a = kb.IsKeyPressed(KeyCode::KEY_A);
    bool s = kb.IsKeyPressed(KeyCode::KEY_S);
    bool d = kb.IsKeyPressed(KeyCode::KEY_D);
    
    // Process movement based on combination
    if (w && a) {
        player.MoveForwardLeft();
    } else if (w && d) {
        player.MoveForwardRight();
    } else if (w) {
        player.MoveForward();
    }
    // ... etc
}
```

---

## Platform-Specific Considerations

### Windows-Specific Features

```cpp
#ifdef PLATFORM_WINDOWS
void WindowsSpecificSetup() {
    KeyboardConfig config;
    
    // Block Windows key to prevent accidental start menu
    config.enableHotKeyBlocking = true;
    
    // Windows-specific hotkeys
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    kb.Initialize(config);
    
    // Register Windows-specific combinations
    kb.RegisterHotkey({KeyCode::KEY_WIN_LEFT, KeyCode::KEY_D}, []() {
        // Minimize all windows (show desktop)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Windows+D pressed - minimizing game");
        game.MinimizeWindow();
    });
    
    // Handle Alt+F4 gracefully
    kb.RegisterHotkey({KeyCode::KEY_ALT_LEFT, KeyCode::KEY_F4}, []() {
        game.RequestGracefulShutdown();
    });
}
#endif
```

### Linux-Specific Features

```cpp
#ifdef PLATFORM_LINUX
void LinuxSpecificSetup() {
    // Linux typically has different modifier key behavior
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Register Linux-specific combinations
    kb.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_ALT_LEFT, KeyCode::KEY_T}, []() {
        // Ctrl+Alt+T might open terminal - handle appropriately
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Terminal shortcut detected");
    });
}
#endif
```

### macOS-Specific Features

```cpp
#ifdef PLATFORM_MACOS
void MacOSSpecificSetup() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // macOS uses Command key instead of Ctrl for most shortcuts
    kb.RegisterHotkey({KeyCode::KEY_MACOS_COMMAND, KeyCode::KEY_Q}, []() {
        // Command+Q to quit (macOS standard)
        game.RequestGracefulShutdown();
    });
    
    kb.RegisterHotkey({KeyCode::KEY_MACOS_COMMAND, KeyCode::KEY_S}, []() {
        // Command+S to save (macOS standard)
        game.SaveGame();
    });
    
    // Handle macOS-specific function keys
    kb.RegisterHotkey({KeyCode::KEY_MACOS_FN, KeyCode::KEY_F11}, []() {
        // Fn+F11 might control volume - handle gracefully
        debug.logLevelMessage(LogLevel::LOG_INFO, L"macOS volume shortcut detected");
    });
}
#endif
```

### Mobile Platform Considerations

```cpp
#ifdef PLATFORM_ANDROID
void AndroidSpecificSetup() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Android hardware keys
    kb.RegisterHotkey({KeyCode::KEY_ANDROID_BACK}, []() {
        // Android back button
        if (game.CanGoBack()) {
            game.GoBack();
        } else {
            game.ShowExitConfirmation();
        }
    });
    
    kb.RegisterHotkey({KeyCode::KEY_ANDROID_HOME}, []() {
        // Android home button - minimize gracefully
        game.PauseAndMinimize();
    });
    
    kb.RegisterHotkey({KeyCode::KEY_ANDROID_MENU}, []() {
        // Android menu button
        game.OpenContextMenu();
    });
}
#endif

#ifdef PLATFORM_IOS
void IOSSpecificSetup() {
    // iOS keyboard handling is typically done through UIKit
    // This is a placeholder for potential hardware keyboard support
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // iOS may have external keyboard support
    kb.RegisterHotkey({KeyCode::KEY_ESCAPE}, []() {
        game.OpenPauseMenu();
    });
}
#endif
```

---

## Error Handling and Debugging

### Comprehensive Error Handling

```cpp
class KeyboardManager {
private:
    KeyboardHandler& keyboard;
    bool isInitialized;
    
public:
    KeyboardManager() : keyboard(KeyboardHandler::GetInstance()), isInitialized(false) {}
    
    bool Initialize() {
        try {
            // Attempt initialization with retry logic
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (keyboard.Initialize()) {
                    isInitialized = true;
                    debug.logLevelMessage(LogLevel::LOG_INFO, 
                        L"Keyboard initialized successfully on attempt %d", attempt + 1);
                    return true;
                }
                
                debug.logDebugMessage(LogLevel::LOG_WARNING, 
                    L"Keyboard initialization failed, attempt %d/3", attempt + 1);
                
                // Wait before retry
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"Keyboard initialization failed after 3 attempts");
            return false;
            
        } catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, 
                L"Exception during keyboard initialization: %S", e.what());
            return false;
        }
    }
    
    bool EnableSafely() {
        if (!isInitialized) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"Cannot enable keyboard - not initialized");
            return false;
        }
        
        try {
            if (!keyboard.EnableKeyboardSystem()) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, 
                    L"Failed to enable keyboard system");
                return false;
            }
            
            // Verify system is actually enabled
            if (!keyboard.IsKeyboardSystemEnabled()) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, 
                    L"Keyboard system reports as not enabled after enable call");
                return false;
            }
            
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Keyboard system enabled successfully");
            return true;
            
        } catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, 
                L"Exception enabling keyboard system: %S", e.what());
            return false;
        }
    }
    
    void SafeCleanup() {
        try {
            if (keyboard.IsKeyboardSystemEnabled()) {
                keyboard.DisableKeyboardSystem();
            }
            
            if (isInitialized) {
                keyboard.Cleanup();
                isInitialized = false;
            }
            
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Keyboard cleanup completed successfully");
                
        } catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, 
                L"Exception during keyboard cleanup: %S", e.what());
        }
    }
    
    ~KeyboardManager() {
        SafeCleanup();
    }
};
```

### Debug Output and Logging

```cpp
void EnableKeyboardDebugging() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Set up comprehensive logging for debugging
    kb.SetKeyDownHandler([](KeyCode keyCode, uint32_t modifierFlags) {
        std::string keyName = KeyboardHandler::GetInstance().KeyCodeToString(keyCode);
        std::string modifiers = ModifierHelper::ModifiersToString(modifierFlags);
        
        if (!modifiers.empty()) {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[DEBUG] Key DOWN: %S+%S", modifiers.c_str(), keyName.c_str());
        } else {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"[DEBUG] Key DOWN: %S", keyName.c_str());
        }
    });
    
    kb.SetKeyUpHandler([](KeyCode keyCode, uint32_t modifierFlags) {
        std::string keyName = KeyboardHandler::GetInstance().KeyCodeToString(keyCode);
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[DEBUG] Key UP: %S", keyName.c_str());
    });
    
    kb.SetKeyComboHandler([](const std::vector<KeyCode>& keys, uint32_t modifierFlags) {
        std::string combo;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i > 0) combo += "+";
            combo += KeyboardHandler::GetInstance().KeyCodeToString(keys[i]);
        }
        
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"[DEBUG] Key COMBO: %S", combo.c_str());
    });
}
```

### Diagnostic Functions

```cpp
void RunKeyboardDiagnostics() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Keyboard System Diagnostics ===");
    
    // Check initialization status
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Initialized: %s", kb.IsInitialized() ? L"Yes" : L"No");
    
    // Check system enabled status
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"System Enabled: %s", kb.IsKeyboardSystemEnabled() ? L"Yes" : L"No");
    
    // Check thread status
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Thread Running: %s", kb.IsKeyboardThreadRunning() ? L"Yes" : L"No");
    
    // Check lock key states
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Caps Lock: %s, Num Lock: %s, Scroll Lock: %s",
        kb.IsCapsLockOn() ? L"ON" : L"OFF",
        kb.IsNumLockOn() ? L"ON" : L"OFF",
        kb.IsScrollLockOn() ? L"ON" : L"OFF");
    
    // Get performance metrics
    float cpuUsage;
    uint64_t memoryUsage;
    if (kb.GetThreadPerformanceMetrics(cpuUsage, memoryUsage)) {
        debug.logDebugMessage(LogLevel::LOG_INFO, 
            L"Performance - CPU: %.1f%%, Memory: %llu bytes",
            cpuUsage * 100.0f, memoryUsage);
    }
    
    // Get processing statistics
    uint64_t eventsProcessed;
    float avgProcessingTime;
    kb.GetKeyboardStats(eventsProcessed, avgProcessingTime);
    
    debug.logDebugMessage(LogLevel::LOG_INFO, 
        L"Statistics - Events: %llu, Avg time: %.3f μs, Keys logged: %llu",
        eventsProcessed, avgProcessingTime, kb.GetTotalKeysLogged());
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== End Diagnostics ===");
}
```

---

## Complete Example Applications

### Example 1: Basic Game Input System

```cpp
class GameInputSystem {
private:
    KeyboardHandler& keyboard;
    Player& player;
    GameState& gameState;
    
public:
    GameInputSystem(Player& p, GameState& gs) 
        : keyboard(KeyboardHandler::GetInstance()), player(p), gameState(gs) {}
    
    bool Initialize() {
        // Initialize keyboard with gaming configuration
        KeyboardConfig config;
        config.enableKeyLogging = false;       // Disable for performance
        config.enableHotKeyBlocking = true;    // Block OS shortcuts
        config.enableKeyRepeat = false;        // Disable for precise input
        config.maxCombinationKeys = 4;         // Support 4-key combinations
        
        if (!keyboard.Initialize(config)) {
            return false;
        }
        
        if (!keyboard.EnableKeyboardSystem()) {
            return false;
        }
        
        // Set up game-specific hotkeys
        SetupGameHotkeys();
        
        return true;
    }
    
    void ProcessInput() {
        // Update key states for edge detection
        keyboard.UpdateKeyStates();
        
        // Process movement (continuous)
        ProcessMovement();
        
        // Process actions (edge-triggered)
        ProcessActions();
        
        // Process camera control
        ProcessCamera();
    }
    
private:
    void SetupGameHotkeys() {
        // Pause game
        keyboard.RegisterHotkey({KeyCode::KEY_ESCAPE}, [this]() {
            gameState.TogglePause();
        });
        
        // Quick save
        keyboard.RegisterHotkey({KeyCode::KEY_F5}, [this]() {
            gameState.QuickSave();
        });
        
        // Quick load
        keyboard.RegisterHotkey({KeyCode::KEY_F9}, [this]() {
            gameState.QuickLoad();
        });
        
        // Toggle inventory
        keyboard.RegisterHotkey({KeyCode::KEY_I}, [this]() {
            gameState.ToggleInventory();
        });
        
        // Screenshot
        keyboard.RegisterHotkey({KeyCode::KEY_F12}, [this]() {
            gameState.TakeScreenshot();
        });
    }
    
    void ProcessMovement() {
        Vector3 movement(0, 0, 0);
        
        // Basic WASD movement
        if (keyboard.IsKeyPressed(KeyCode::KEY_W)) movement.z += 1.0f;
        if (keyboard.IsKeyPressed(KeyCode::KEY_S)) movement.z -= 1.0f;
        if (keyboard.IsKeyPressed(KeyCode::KEY_A)) movement.x -= 1.0f;
        if (keyboard.IsKeyPressed(KeyCode::KEY_D)) movement.x += 1.0f;
        
        // Vertical movement
        if (keyboard.IsKeyPressed(KeyCode::KEY_SPACE)) movement.y += 1.0f;
        if (keyboard.IsKeyPressed(KeyCode::KEY_C)) movement.y -= 1.0f;
        
        // Apply movement with speed modifier
        float speed = 1.0f;
        if (keyboard.IsKeyPressed(KeyCode::KEY_SHIFT_LEFT)) {
            speed = 2.0f;  // Run
        } else if (keyboard.IsKeyPressed(KeyCode::KEY_CTRL_LEFT)) {
            speed = 0.3f;  // Walk slowly
        }
        
        player.Move(movement * speed);
    }
    
    void ProcessActions() {
        // Jump (only on press, not hold)
        if (keyboard.IsKeyJustPressed(KeyCode::KEY_SPACE)) {
            player.Jump();
        }
        
        // Interact
        if (keyboard.IsKeyJustPressed(KeyCode::KEY_E)) {
            player.Interact();
        }
        
        // Attack
        if (keyboard.IsKeyJustPressed(KeyCode::KEY_ENTER)) {
            player.Attack();
        }
        
        // Use item
        if (keyboard.IsKeyJustPressed(KeyCode::KEY_R)) {
            player.UseItem();
        }
        
        // Crouch toggle
        if (keyboard.IsKeyJustPressed(KeyCode::KEY_CTRL_LEFT)) {
            player.ToggleCrouch();
        }
    }
    
    void ProcessCamera() {
        // Camera rotation with arrow keys
        Vector2 cameraRotation(0, 0);
        
        if (keyboard.IsKeyPressed(KeyCode::KEY_ARROW_LEFT)) cameraRotation.x -= 1.0f;
        if (keyboard.IsKeyPressed(KeyCode::KEY_ARROW_RIGHT)) cameraRotation.x += 1.0f;
        if (keyboard.IsKeyPressed(KeyCode::KEY_ARROW_UP)) cameraRotation.y += 1.0f;
        if (keyboard.IsKeyPressed(KeyCode::KEY_ARROW_DOWN)) cameraRotation.y -= 1.0f;
        
        if (cameraRotation.x != 0 || cameraRotation.y != 0) {
            player.RotateCamera(cameraRotation);
        }
    }
    
public:
    ~GameInputSystem() {
        keyboard.DisableKeyboardSystem();
        keyboard.Cleanup();
    }
};
```

### Example 2: Advanced Combat System

```cpp
class CombatInputSystem {
private:
    KeyboardHandler& keyboard;
    CombatManager& combat;
    std::chrono::steady_clock::time_point lastComboTime;
    std::vector<KeyCode> comboSequence;
    
public:
    CombatInputSystem(CombatManager& cm) 
        : keyboard(KeyboardHandler::GetInstance()), combat(cm) {
        lastComboTime = std::chrono::steady_clock::now();
    }
    
    bool Initialize() {
        KeyboardConfig config;
        config.enableKeyLogging = true;        // Enable for combo analysis
        config.enableMultiKeyDetection = true; // Enable for complex combos
        config.maxCombinationKeys = 6;         // Support complex combinations
        
        if (!keyboard.Initialize(config)) {
            return false;
        }
        
        // Set up combat-specific handlers
        keyboard.SetKeyDownHandler([this](KeyCode key, uint32_t modifiers) {
            HandleCombatInput(key, modifiers);
        });
        
        // Register special moves
        RegisterSpecialMoves();
        
        return keyboard.EnableKeyboardSystem();
    }
    
private:
    void RegisterSpecialMoves() {
        // Fireball: Down, Down-Forward, Forward + Punch (A)
        keyboard.RegisterHotkey({KeyCode::KEY_S, KeyCode::KEY_D, KeyCode::KEY_A}, [this]() {
            combat.ExecuteSpecialMove("Fireball");
        });
        
        // Dragon Punch: Forward, Down, Down-Forward + Punch (A)
        keyboard.RegisterHotkey({KeyCode::KEY_D, KeyCode::KEY_S, KeyCode::KEY_A}, [this]() {
            combat.ExecuteSpecialMove("DragonPunch");
        });
        
        // Super Move: requires holding two punch buttons
        keyboard.RegisterHotkey({KeyCode::KEY_A, KeyCode::KEY_S, KeyCode::KEY_D}, [this]() {
            if (keyboard.GetKeyHoldDuration(KeyCode::KEY_A) > 1000 &&
                keyboard.GetKeyHoldDuration(KeyCode::KEY_S) > 1000) {
                combat.ExecuteSpecialMove("SuperMove");
            }
        });
        
        // Block (hold)
        keyboard.SetKeyDownHandler([this](KeyCode key, uint32_t modifiers) {
            if (key == KeyCode::KEY_SHIFT_LEFT) {
                combat.StartBlocking();
            }
        });
        
        keyboard.SetKeyUpHandler([this](KeyCode key, uint32_t modifiers) {
            if (key == KeyCode::KEY_SHIFT_LEFT) {
                combat.StopBlocking();
            }
        });
    }
    
    void HandleCombatInput(KeyCode key, uint32_t modifiers) {
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastInput = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastComboTime).count();
        
        // Reset combo if too much time has passed
        if (timeSinceLastInput > 500) { // 500ms combo window
            comboSequence.clear();
        }
        
        // Add to combo sequence
        comboSequence.push_back(key);
        lastComboTime = now;
        
        // Process basic attacks
        switch (key) {
            case KeyCode::KEY_A:
                if (modifiers == 0) {
                    combat.LightPunch();
                } else if (ModifierHelper::IsShiftPressed(modifiers)) {
                    combat.HeavyPunch();
                }
                break;
                
            case KeyCode::KEY_S:
                if (modifiers == 0) {
                    combat.LightKick();
                } else if (ModifierHelper::IsShiftPressed(modifiers)) {
                    combat.HeavyKick();
                }
                break;
                
            case KeyCode::KEY_D:
                combat.Dodge();
                break;
        }
        
        // Check for combo moves
        CheckCombos();
        
        // Limit combo sequence length
        if (comboSequence.size() > 8) {
            comboSequence.erase(comboSequence.begin());
        }
    }
    
    void CheckCombos() {
        // Check for specific combo patterns
        if (comboSequence.size() >= 3) {
            // Check last 3 inputs
            auto recent = std::vector<KeyCode>(comboSequence.end() - 3, comboSequence.end());
            
            // Combo 1: A-A-S (Punch-Punch-Kick)
            if (recent[0] == KeyCode::KEY_A && 
                recent[1] == KeyCode::KEY_A && 
                recent[2] == KeyCode::KEY_S) {
                combat.ExecuteCombo("PunchPunchKick");
                comboSequence.clear();
            }
            
            // Combo 2: S-D-A (Kick-Dodge-Punch)
            else if (recent[0] == KeyCode::KEY_S && 
                     recent[1] == KeyCode::KEY_D && 
                     recent[2] == KeyCode::KEY_A) {
                combat.ExecuteCombo("KickDodgePunch");
                comboSequence.clear();
            }
        }
        
        // Check for 4-hit combo
        if (comboSequence.size() >= 4) {
            auto recent = std::vector<KeyCode>(comboSequence.end() - 4, comboSequence.end());
            
            // Ultimate combo: A-S-A-S
            if (recent[0] == KeyCode::KEY_A && 
                recent[1] == KeyCode::KEY_S && 
                recent[2] == KeyCode::KEY_A && 
                recent[3] == KeyCode::KEY_S) {
                combat.ExecuteCombo("UltimateCombo");
                comboSequence.clear();
            }
        }
    }
};
```

### Example 3: Text Editor with Shortcuts

```cpp
class TextEditorInputSystem {
private:
    KeyboardHandler& keyboard;
    TextEditor& editor;
    bool ctrlPressed = false;
    bool shiftPressed = false;
    
public:
    TextEditorInputSystem(TextEditor& te) 
        : keyboard(KeyboardHandler::GetInstance()), editor(te) {}
    
    bool Initialize() {
        KeyboardConfig config;
        config.enableKeyLogging = true;        // For undo/redo history
        config.enableKeyRepeat = true;         // Allow key repeat for typing
        config.keyRepeatDelay = 500;          // Standard typing repeat
        config.keyRepeatRate = 30;            // Fast repeat for text
        
        if (!keyboard.Initialize(config)) {
            return false;
        }
        
        // Set up text editing handlers
        SetupTextHandlers();
        SetupShortcuts();
        
        return keyboard.EnableKeyboardSystem();
    }
    
private:
    void SetupTextHandlers() {
        keyboard.SetKeyDownHandler([this](KeyCode key, uint32_t modifiers) {
            ctrlPressed = ModifierHelper::IsCtrlPressed(modifiers);
            shiftPressed = ModifierHelper::IsShiftPressed(modifiers);
            
            HandleTextInput(key, modifiers);
        });
        
        keyboard.SetKeyUpHandler([this](KeyCode key, uint32_t modifiers) {
            ctrlPressed = ModifierHelper::IsCtrlPressed(modifiers);
            shiftPressed = ModifierHelper::IsShiftPressed(modifiers);
        });
    }
    
    void SetupShortcuts() {
        // Standard text editing shortcuts
        
        // Ctrl+C (Copy)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_C}, [this]() {
            editor.Copy();
        });
        
        // Ctrl+V (Paste)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_V}, [this]() {
            editor.Paste();
        });
        
        // Ctrl+X (Cut)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_X}, [this]() {
            editor.Cut();
        });
        
        // Ctrl+Z (Undo)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_Z}, [this]() {
            editor.Undo();
        });
        
        // Ctrl+Y (Redo)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_Y}, [this]() {
            editor.Redo();
        });
        
        // Ctrl+A (Select All)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_A}, [this]() {
            editor.SelectAll();
        });
        
        // Ctrl+S (Save)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_S}, [this]() {
            editor.Save();
        });
        
        // Ctrl+O (Open)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_O}, [this]() {
            editor.Open();
        });
        
        // Ctrl+N (New)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_N}, [this]() {
            editor.New();
        });
        
        // Ctrl+F (Find)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_F}, [this]() {
            editor.OpenFindDialog();
        });
        
        // Ctrl+H (Replace)
        keyboard.RegisterHotkey({KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_H}, [this]() {
            editor.OpenReplaceDialog();
        });
    }
    
    void HandleTextInput(KeyCode key, uint32_t modifiers) {
        // Don't process text if Ctrl is pressed (for shortcuts)
        if (ctrlPressed) {
            return;
        }
        
        // Handle special keys
        switch (key) {
            case KeyCode::KEY_BACKSPACE:
                if (shiftPressed) {
                    editor.DeleteLine();
                } else {
                    editor.Backspace();
                }
                break;
                
            case KeyCode::KEY_DELETE:
                if (shiftPressed) {
                    editor.Cut();
                } else {
                    editor.Delete();
                }
                break;
                
            case KeyCode::KEY_ENTER:
                editor.InsertNewline();
                break;
                
            case KeyCode::KEY_TAB:
                if (shiftPressed) {
                    editor.Unindent();
                } else {
                    editor.Indent();
                }
                break;
                
            case KeyCode::KEY_HOME:
                if (shiftPressed) {
                    editor.SelectToLineStart();
                } else {
                    editor.MoveToLineStart();
                }
                break;
                
            case KeyCode::KEY_END:
                if (shiftPressed) {
                    editor.SelectToLineEnd();
                } else {
                    editor.MoveToLineEnd();
                }
                break;
                
            case KeyCode::KEY_ARROW_LEFT:
                if (shiftPressed) {
                    editor.ExtendSelectionLeft();
                } else {
                    editor.MoveCursorLeft();
                }
                break;
                
            case KeyCode::KEY_ARROW_RIGHT:
                if (shiftPressed) {
                    editor.ExtendSelectionRight();
                } else {
                    editor.MoveCursorRight();
                }
                break;
                
            case KeyCode::KEY_ARROW_UP:
                if (shiftPressed) {
                    editor.ExtendSelectionUp();
                } else {
                    editor.MoveCursorUp();
                }
                break;
                
            case KeyCode::KEY_ARROW_DOWN:
                if (shiftPressed) {
                    editor.ExtendSelectionDown();
                } else {
                    editor.MoveCursorDown();
                }
                break;
                
            case KeyCode::KEY_PAGE_UP:
                editor.PageUp();
                break;
                
            case KeyCode::KEY_PAGE_DOWN:
                editor.PageDown();
                break;
                
            default:
                // Handle regular character input
                char character = KeyCodeToChar(key, shiftPressed);
                if (character != 0) {
                    editor.InsertCharacter(character);
                }
                break;
        }
    }
    
    char KeyCodeToChar(KeyCode key, bool shiftPressed) {
        // Convert KeyCode to actual character
        switch (key) {
            // Letters
            case KeyCode::KEY_A: return shiftPressed ? 'A' : 'a';
            case KeyCode::KEY_B: return shiftPressed ? 'B' : 'b';
            case KeyCode::KEY_C: return shiftPressed ? 'C' : 'c';
            case KeyCode::KEY_D: return shiftPressed ? 'D' : 'd';
            case KeyCode::KEY_E: return shiftPressed ? 'E' : 'e';
            case KeyCode::KEY_F: return shiftPressed ? 'F' : 'f';
            case KeyCode::KEY_G: return shiftPressed ? 'G' : 'g';
            case KeyCode::KEY_H: return shiftPressed ? 'H' : 'h';
            case KeyCode::KEY_I: return shiftPressed ? 'I' : 'i';
            case KeyCode::KEY_J: return shiftPressed ? 'J' : 'j';
            case KeyCode::KEY_K: return shiftPressed ? 'K' : 'k';
            case KeyCode::KEY_L: return shiftPressed ? 'L' : 'l';
            case KeyCode::KEY_M: return shiftPressed ? 'M' : 'm';
            case KeyCode::KEY_N: return shiftPressed ? 'N' : 'n';
            case KeyCode::KEY_O: return shiftPressed ? 'O' : 'o';
            case KeyCode::KEY_P: return shiftPressed ? 'P' : 'p';
            case KeyCode::KEY_Q: return shiftPressed ? 'Q' : 'q';
            case KeyCode::KEY_R: return shiftPressed ? 'R' : 'r';
            case KeyCode::KEY_S: return shiftPressed ? 'S' : 's';
            case KeyCode::KEY_T: return shiftPressed ? 'T' : 't';
            case KeyCode::KEY_U: return shiftPressed ? 'U' : 'u';
            case KeyCode::KEY_V: return shiftPressed ? 'V' : 'v';
            case KeyCode::KEY_W: return shiftPressed ? 'W' : 'w';
            case KeyCode::KEY_X: return shiftPressed ? 'X' : 'x';
            case KeyCode::KEY_Y: return shiftPressed ? 'Y' : 'y';
            case KeyCode::KEY_Z: return shiftPressed ? 'Z' : 'z';
            
            // Numbers
            case KeyCode::KEY_0: return shiftPressed ? ')' : '0';
            case KeyCode::KEY_1: return shiftPressed ? '!' : '1';
            case KeyCode::KEY_2: return shiftPressed ? '@' : '2';
            case KeyCode::KEY_3: return shiftPressed ? '#' : '3';
            case KeyCode::KEY_4: return shiftPressed ? '
         : '4';
            case KeyCode::KEY_5: return shiftPressed ? '%' : '5';
            case KeyCode::KEY_6: return shiftPressed ? '^' : '6';
            case KeyCode::KEY_7: return shiftPressed ? '&' : '7';
            case KeyCode::KEY_8: return shiftPressed ? '*' : '8';
            case KeyCode::KEY_9: return shiftPressed ? '(' : '9';
            
            // Special characters
            case KeyCode::KEY_SPACE: return ' ';
            case KeyCode::KEY_SEMICOLON: return shiftPressed ? ':' : ';';
            case KeyCode::KEY_EQUALS: return shiftPressed ? '+' : '=';
            case KeyCode::KEY_COMMA: return shiftPressed ? '<' : ',';
            case KeyCode::KEY_MINUS: return shiftPressed ? '_' : '-';
            case KeyCode::KEY_PERIOD: return shiftPressed ? '>' : '.';
            case KeyCode::KEY_SLASH: return shiftPressed ? '?' : '/';
            case KeyCode::KEY_GRAVE: return shiftPressed ? '~' : '`';
            case KeyCode::KEY_BRACKET_LEFT: return shiftPressed ? '{' : '[';
            case KeyCode::KEY_BACKSLASH: return shiftPressed ? '|' : '\\';
            case KeyCode::KEY_BRACKET_RIGHT: return shiftPressed ? '}' : ']';
            case KeyCode::KEY_QUOTE: return shiftPressed ? '"' : '\'';
            
            default:
                return 0; // Non-printable character
        }
    }
};
```

---

## Best Practices

### Performance Optimization

```cpp
// 1. Use lock-free operations for frequent queries
void OptimizedGameLoop() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Update once per frame
    kb.UpdateKeyStates();
    
    // Use direct key state queries (lock-free)
    bool moving = kb.IsKeyPressed(KeyCode::KEY_W) || 
                  kb.IsKeyPressed(KeyCode::KEY_A) ||
                  kb.IsKeyPressed(KeyCode::KEY_S) ||
                  kb.IsKeyPressed(KeyCode::KEY_D);
                  
    if (moving) {
        ProcessMovement();
    }
    
    // Use edge detection for actions
    if (kb.IsKeyJustPressed(KeyCode::KEY_SPACE)) {
        player.Jump();
    }
}

// 2. Minimize callback complexity
void SetupEfficientHandlers() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Keep handlers simple and fast
    kb.SetKeyDownHandler([](KeyCode key, uint32_t modifiers) {
        // Minimal processing - defer heavy work to game loop
        g_inputEvents.push({key, true, modifiers});
    });
    
    // Process events in batch during game loop
    void ProcessInputEvents() {
        while (!g_inputEvents.empty()) {
            InputEvent event = g_inputEvents.front();
            g_inputEvents.pop();
            
            // Process event with full game context
            HandleInputEvent(event);
        }
    }
}

// 3. Use appropriate configuration for your needs
void ConfigureForPerformance() {
    KeyboardConfig config;
    
    // Disable unused features
    config.enableKeyLogging = false;       // Only enable if needed for AI
    config.enableKeyRepeat = false;        // Disable if not needed
    config.maxCombinationKeys = 3;         // Reduce if complex combos not needed
    
    // Enable only what you need
    config.enableHotKeyBlocking = true;    // Usually desired for games
    config.enableMultiKeyDetection = true; // Keep for shortcuts
    
    KeyboardHandler::GetInstance().Initialize(config);
}
```

### Memory Management

```cpp
// 1. Proper cleanup in destructors
class InputManager {
    KeyboardHandler& keyboard;
    
public:
    InputManager() : keyboard(KeyboardHandler::GetInstance()) {
        // Initialize
    }
    
    ~InputManager() {
        // Always clean up properly
        keyboard.DisableKeyboardSystem();
        keyboard.Cleanup();
    }
};

// 2. Manage key log size for AI systems
void ManageKeyLogMemory() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Periodically clear old logs
    static auto lastClear = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    
    if (std::chrono::duration_cast<std::chrono::minutes>(now - lastClear).count() > 5) {
        kb.ClearKeyLog();
        lastClear = now;
    }
}

// 3. Efficient hotkey management
class HotkeyScope {
    std::vector<std::vector<KeyCode>> hotkeys;
    KeyboardHandler& kb;
    
public:
    HotkeyScope() : kb(KeyboardHandler::GetInstance()) {}
    
    void RegisterScoped(const std::vector<KeyCode>& keys, std::function<void()> callback) {
        if (kb.RegisterHotkey(keys, callback)) {
            hotkeys.push_back(keys);
        }
    }
    
    ~HotkeyScope() {
        // Automatically unregister all hotkeys
        for (const auto& hotkey : hotkeys) {
            kb.UnregisterHotkey(hotkey);
        }
    }
};
```

### Error Handling Best Practices

```cpp
// 1. Robust initialization with fallbacks
bool InitializeInputSystemSafely() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    try {
        // Try optimal configuration first
        KeyboardConfig optimalConfig;
        optimalConfig.enableKeyLogging = true;
        optimalConfig.enableHotKeyBlocking = true;
        optimalConfig.maxCombinationKeys = 6;
        
        if (kb.Initialize(optimalConfig) && kb.EnableKeyboardSystem()) {
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Keyboard initialized with optimal configuration");
            return true;
        }
        
        // Fall back to minimal configuration
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
            L"Optimal keyboard config failed, trying minimal config");
            
        KeyboardConfig minimalConfig;
        minimalConfig.enableKeyLogging = false;
        minimalConfig.enableHotKeyBlocking = false;
        minimalConfig.maxCombinationKeys = 2;
        
        if (kb.Initialize(minimalConfig) && kb.EnableKeyboardSystem()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"Keyboard initialized with minimal configuration");
            return true;
        }
        
        debug.logLevelMessage(LogLevel::LOG_ERROR, 
            L"Failed to initialize keyboard with any configuration");
        return false;
        
    } catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, 
            L"Exception during keyboard initialization: %S", e.what());
        return false;
    }
}

// 2. Safe event handler registration
void RegisterHandlersSafely() {
    try {
        KeyboardHandler& kb = KeyboardHandler::GetInstance();
        
        // Wrap handlers in try-catch to prevent crashes
        kb.SetKeyDownHandler([](KeyCode key, uint32_t modifiers) {
            try {
                ProcessKeyDown(key, modifiers);
            } catch (const std::exception& e) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, 
                    L"Exception in key down handler: %S", e.what());
            }
        });
        
        kb.SetKeyUpHandler([](KeyCode key, uint32_t modifiers) {
            try {
                ProcessKeyUp(key, modifiers);
            } catch (const std::exception& e) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, 
                    L"Exception in key up handler: %S", e.what());
            }
        });
        
    } catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, 
            L"Exception registering keyboard handlers: %S", e.what());
    }
}

// 3. Graceful degradation
void HandleKeyboardFailure() {
    // If keyboard system fails, provide alternative input methods
    debug.logLevelMessage(LogLevel::LOG_WARNING, 
        L"Keyboard system failed - enabling alternative input");
    
    // Enable mouse-only mode
    game.SetMouseOnlyMode(true);
    
    // Show on-screen controls
    ui.ShowVirtualKeyboard(true);
    
    // Notify user
    ui.ShowNotification("Keyboard input unavailable - using alternative controls");
}
```

### Thread Safety Guidelines

```cpp
// 1. Proper thread coordination
class ThreadSafeInputProcessor {
private:
    KeyboardHandler& keyboard;
    std::atomic<bool> processing{false};
    
public:
    void StartProcessing() {
        // Ensure only one thread processes input
        bool expected = false;
        if (processing.compare_exchange_strong(expected, true)) {
            
            // Start keyboard thread
            if (!keyboard.StartKeyboardThread()) {
                processing.store(false);
                throw std::runtime_error("Failed to start keyboard thread");
            }
        }
    }
    
    void StopProcessing() {
        if (processing.load()) {
            keyboard.StopKeyboardThread();
            processing.store(false);
        }
    }
    
    ~ThreadSafeInputProcessor() {
        StopProcessing();
    }
};

// 2. Avoid shared state in handlers
void SetupThreadSafeHandlers() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Use thread-safe mechanisms for communication
    kb.SetKeyDownHandler([](KeyCode key, uint32_t modifiers) {
        // Use atomic or thread-safe containers
        g_inputQueue.push({key, true, modifiers});
        g_inputAvailable.notify_one();
    });
    
    // Process in main thread
    void ProcessInputFromQueue() {
        InputEvent event;
        while (g_inputQueue.try_pop(event)) {
            HandleInputEvent(event);
        }
    }
}
```

---

## Troubleshooting

### Common Issues and Solutions

#### Issue 1: Keyboard Not Working
```cpp
void DiagnoseKeyboardIssue() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Keyboard Diagnostic ===");
    
    // Check initialization
    if (!kb.IsInitialized()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, 
            L"PROBLEM: Keyboard not initialized");
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"SOLUTION: Call kb.Initialize() before use");
        return;
    }
    
    // Check if enabled
    if (!kb.IsKeyboardSystemEnabled()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, 
            L"PROBLEM: Keyboard system not enabled");
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"SOLUTION: Call kb.EnableKeyboardSystem()");
        return;
    }
    
    // Check thread status
    if (!kb.IsKeyboardThreadRunning()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
            L"WARNING: Keyboard thread not running");
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"SOLUTION: Call kb.StartKeyboardThread()");
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard system appears to be working");
}
```

#### Issue 2: Keys Not Responding
```cpp
void TestKeyResponsiveness() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Set up test handler
    bool testKeyPressed = false;
    
    kb.SetKeyDownHandler([&testKeyPressed](KeyCode key, uint32_t modifiers) {
        if (key == KeyCode::KEY_F1) {
            testKeyPressed = true;
            debug.logLevelMessage(LogLevel::LOG_INFO, L"F1 key detected!");
        }
    });
    
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"Press F1 to test keyboard responsiveness...");
    
    // Wait for test
    auto startTime = std::chrono::steady_clock::now();
    while (!testKeyPressed) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();
            
        if (elapsed > 10) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                L"No F1 key detected after 10 seconds - keyboard may not be working");
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

#### Issue 3: Performance Problems
```cpp
void DiagnosePerformanceIssue() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    float cpuUsage;
    uint64_t memoryUsage;
    
    if (kb.GetThreadPerformanceMetrics(cpuUsage, memoryUsage)) {
        // Check CPU usage
        if (cpuUsage > 0.05f) { // More than 5%
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"HIGH CPU USAGE: Keyboard thread using %.1f%% CPU", cpuUsage * 100.0f);
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"SOLUTIONS: Disable key logging, reduce max combination keys, or check for busy handlers");
        }
        
        // Check memory usage
        if (memoryUsage > 512 * 1024) { // More than 512KB
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"HIGH MEMORY USAGE: Keyboard system using %llu bytes", memoryUsage);
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"SOLUTIONS: Clear key log periodically, reduce key log size, or check for memory leaks");
        }
        
        // Check processing stats
        uint64_t eventsProcessed;
        float avgTime;
        kb.GetKeyboardStats(eventsProcessed, avgTime);
        
        if (avgTime > 100.0f) { // More than 100 microseconds
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"SLOW PROCESSING: Average event processing time %.3f μs", avgTime);
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"SOLUTIONS: Simplify event handlers, reduce combination complexity");
        }
    }
}
```

#### Issue 4: Hotkeys Not Working
```cpp
void TestHotkeySystem() {
    KeyboardHandler& kb = KeyboardHandler::GetInstance();
    
    // Test simple hotkey
    std::vector<KeyCode> testHotkey = {KeyCode::KEY_CTRL_LEFT, KeyCode::KEY_F1};
    bool hotkeyTriggered = false;
    
    bool registered = kb.RegisterHotkey(testHotkey, [&hotkeyTriggered]() {
        hotkeyTriggered = true;
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Test hotkey (Ctrl+F1) triggered!");
    });
    
    if (!registered) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, 
            L"PROBLEM: Failed to register test hotkey");
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"SOLUTIONS: Check if multi-key detection is enabled, verify key combination is valid");
        return;
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"Press Ctrl+F1 to test hotkey system...");
    
    // Clean up
    auto cleanup = [&kb, &testHotkey]() {
        kb.UnregisterHotkey(testHotkey);
    };
    
    // Set cleanup for later
    std::atexit([]() {
        // Note: In real code, use proper RAII
    });
}
```

### Platform-Specific Troubleshooting

#### Windows Issues
```cpp
#ifdef PLATFORM_WINDOWS
void DiagnoseWindowsIssues() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Windows-Specific Diagnostics ===");
    
    // Check if running as administrator (may be required for some hooks)
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    
    if (!isAdmin) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
            L"Not running as administrator - some features may not work");
    }
    
    // Check for interfering software
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"If keys are not working, check for:");
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"- Antivirus software blocking keyboard hooks");
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"- Other applications using global keyboard hooks");
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"- Windows Game Mode or Full Screen Optimizations");
}
#endif
```

#### Linux Issues
```cpp
#ifdef PLATFORM_LINUX
void DiagnoseLinuxIssues() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Linux-Specific Diagnostics ===");
    
    // Check X11 connection
    Display* testDisplay = XOpenDisplay(NULL);
    if (!testDisplay) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, 
            L"Cannot connect to X11 display - check DISPLAY environment variable");
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"Try: export DISPLAY=:0");
        return;
    }
    XCloseDisplay(testDisplay);
    
    // Check permissions
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"If keyboard input is not working:");
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"- Ensure your user is in the 'input' group");
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"- Check file permissions on /dev/input/event*");
    debug.logLevelMessage(LogLevel::LOG_INFO, 
        L"- Verify X11 security settings allow input capture");
}
#endif
```

### Debugging Utilities

```cpp
// Real-time keyboard state monitor
class KeyboardDebugMonitor {
private:
    KeyboardHandler& keyboard;
    bool monitoring = false;
    
public:
    KeyboardDebugMonitor() : keyboard(KeyboardHandler::GetInstance()) {}
    
    void StartMonitoring() {
        monitoring = true;
        
        keyboard.SetKeyDownHandler([this](KeyCode key, uint32_t modifiers) {
            if (monitoring) {
                std::string keyName = keyboard.KeyCodeToString(key);
                std::string mods = ModifierHelper::ModifiersToString(modifiers);
                
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"[MONITOR] KEY DOWN: %S%s%S", 
                    mods.empty() ? "" : mods.c_str(),
                    mods.empty() ? "" : "+",
                    keyName.c_str());
            }
        });
        
        keyboard.SetKeyUpHandler([this](KeyCode key, uint32_t modifiers) {
            if (monitoring) {
                std::string keyName = keyboard.KeyCodeToString(key);
                debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                    L"[MONITOR] KEY UP: %S", keyName.c_str());
            }
        });
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
            L"Keyboard monitoring started - press keys to see debug output");
    }
    
    void StopMonitoring() {
        monitoring = false;
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard monitoring stopped");
    }
    
    ~KeyboardDebugMonitor() {
        StopMonitoring();
    }
};

// Usage:
void EnableKeyboardDebugging() {
    static KeyboardDebugMonitor monitor;
    monitor.StartMonitoring();
    
    // Call monitor.StopMonitoring() when done debugging
}
```

---

## Conclusion

The KeyboardHandler class provides a comprehensive, cross-platform solution for keyboard input handling in gaming applications. By following the examples and best practices outlined in this guide, you can:

- **Implement robust keyboard input** with minimal performance overhead
- **Create complex key combinations and hotkeys** for enhanced user experience
- **Integrate with AI systems** through detailed key logging and analysis
- **Handle platform-specific requirements** across Windows, Linux, macOS, and mobile platforms
- **Debug and troubleshoot issues** effectively with built-in diagnostic tools

### Key Takeaways

1. **Always initialize properly**: Use error handling and fallback configurations
2. **Optimize for performance**: Disable unused features and use lock-free operations where possible
3. **Handle cleanup correctly**: Ensure proper resource management in destructors
4. **Plan for cross-platform**: Consider platform-specific behaviors and limitations
5. **Debug systematically**: Use the provided diagnostic tools to identify and resolve issues

### Additional Resources

- Review the `Debug.h` file for logging level configuration
- Check `ThreadManager.h` for thread management details  
- Refer to `ExceptionHandler.h` for comprehensive error handling
- Examine platform-specific documentation for advanced features

The KeyboardHandler class is designed to grow with your application's needs while maintaining optimal performance and reliability across all supported platforms.