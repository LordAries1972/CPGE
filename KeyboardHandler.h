// -------------------------------------------------------------------------------------------------------------
// KeyboardHandler.h - Cross-Platform Singleton Keyboard Handler System
// 
// This class provides comprehensive keyboard input handling across Windows, Linux, macOS, Android, and iOS
// with advanced features including hotkey management, key logging, and thread-safe operations for gaming.
// Designed for optimal performance with minimal locking for real-time game loop integration.
// -------------------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"

#include <vector>
#include <queue>
#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <array>
#include <unordered_map>
#include <functional>

#pragma warning(push)
#pragma warning(disable: 4101)

// Forward declarations
extern Debug debug;
extern ThreadManager threadManager;
extern ExceptionHandler exceptionHandler;

//==============================================================================
// Platform-specific conditional compilation blocks
//==============================================================================
#if defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#include <windows.h>
#include <winuser.h>
#elif defined(__linux__)
#define PLATFORM_LINUX
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#elif defined(__APPLE__)
#define PLATFORM_MACOS
#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
#elif defined(__ANDROID__)
#define PLATFORM_ANDROID
#include <android/input.h>
#include <android/keycodes.h>
#elif defined(__iOS__)
#define PLATFORM_IOS
// iOS uses different input handling through UIKit
#endif

// Maximum number of key presses to log for AI system integration
const uint32_t MAX_KEY_LOG_ENTRIES = 64;

// Thread names for keyboard processing
const char* const KEYBOARD_THREAD_NAME = "KeyboardHandler-Thread";

//==============================================================================
// Comprehensive Key Code Definitions (Cross-Platform)
//==============================================================================
enum class KeyCode : uint32_t {
    // Standard alphanumeric keys
    KEY_A = 0x00000001,                                     // Letter A
    KEY_B = 0x00000002,                                     // Letter B
    KEY_C = 0x00000003,                                     // Letter C
    KEY_D = 0x00000004,                                     // Letter D
    KEY_E = 0x00000005,                                     // Letter E
    KEY_F = 0x00000006,                                     // Letter F
    KEY_G = 0x00000007,                                     // Letter G
    KEY_H = 0x00000008,                                     // Letter H
    KEY_I = 0x00000009,                                     // Letter I
    KEY_J = 0x0000000A,                                     // Letter J
    KEY_K = 0x0000000B,                                     // Letter K
    KEY_L = 0x0000000C,                                     // Letter L
    KEY_M = 0x0000000D,                                     // Letter M
    KEY_N = 0x0000000E,                                     // Letter N
    KEY_O = 0x0000000F,                                     // Letter O
    KEY_P = 0x00000010,                                     // Letter P
    KEY_Q = 0x00000011,                                     // Letter Q
    KEY_R = 0x00000012,                                     // Letter R
    KEY_S = 0x00000013,                                     // Letter S
    KEY_T = 0x00000014,                                     // Letter T
    KEY_U = 0x00000015,                                     // Letter U
    KEY_V = 0x00000016,                                     // Letter V
    KEY_W = 0x00000017,                                     // Letter W
    KEY_X = 0x00000018,                                     // Letter X
    KEY_Y = 0x00000019,                                     // Letter Y
    KEY_Z = 0x0000001A,                                     // Letter Z

    // Number keys (top row)
    KEY_0 = 0x00000030,                                     // Number 0
    KEY_1 = 0x00000031,                                     // Number 1
    KEY_2 = 0x00000032,                                     // Number 2
    KEY_3 = 0x00000033,                                     // Number 3
    KEY_4 = 0x00000034,                                     // Number 4
    KEY_5 = 0x00000035,                                     // Number 5
    KEY_6 = 0x00000036,                                     // Number 6
    KEY_7 = 0x00000037,                                     // Number 7
    KEY_8 = 0x00000038,                                     // Number 8
    KEY_9 = 0x00000039,                                     // Number 9

    // Function keys
    KEY_F1 = 0x00000070,                                    // Function key F1
    KEY_F2 = 0x00000071,                                    // Function key F2
    KEY_F3 = 0x00000072,                                    // Function key F3
    KEY_F4 = 0x00000073,                                    // Function key F4
    KEY_F5 = 0x00000074,                                    // Function key F5
    KEY_F6 = 0x00000075,                                    // Function key F6
    KEY_F7 = 0x00000076,                                    // Function key F7
    KEY_F8 = 0x00000077,                                    // Function key F8
    KEY_F9 = 0x00000078,                                    // Function key F9
    KEY_F10 = 0x00000079,                                   // Function key F10
    KEY_F11 = 0x0000007A,                                   // Function key F11
    KEY_F12 = 0x0000007B,                                   // Function key F12
    KEY_F13 = 0x0000007C,                                   // Function key F13 (extended)
    KEY_F14 = 0x0000007D,                                   // Function key F14 (extended)
    KEY_F15 = 0x0000007E,                                   // Function key F15 (extended)

    // Modifier keys
    KEY_SHIFT_LEFT = 0x000000A0,                            // Left Shift key
    KEY_SHIFT_RIGHT = 0x000000A1,                           // Right Shift key
    KEY_CTRL_LEFT = 0x000000A2,                             // Left Control key
    KEY_CTRL_RIGHT = 0x000000A3,                            // Right Control key
    KEY_ALT_LEFT = 0x000000A4,                              // Left Alt key
    KEY_ALT_RIGHT = 0x000000A5,                             // Right Alt key (AltGr)
    KEY_WIN_LEFT = 0x0000005B,                              // Left Windows/Command key
    KEY_WIN_RIGHT = 0x0000005C,                             // Right Windows/Command key

    // Navigation keys
    KEY_ARROW_UP = 0x00000026,                              // Up arrow key
    KEY_ARROW_DOWN = 0x00000028,                            // Down arrow key
    KEY_ARROW_LEFT = 0x00000025,                            // Left arrow key
    KEY_ARROW_RIGHT = 0x00000027,                           // Right arrow key
    KEY_HOME = 0x00000024,                                  // Home key
    KEY_END = 0x00000023,                                   // End key
    KEY_PAGE_UP = 0x00000021,                               // Page Up key
    KEY_PAGE_DOWN = 0x00000022,                             // Page Down key

    // Special keys
    KEY_SPACE = 0x00000020,                                 // Space bar
    KEY_ENTER = 0x0000010D,                                 // Enter/Return key (changed from 0x0000000D)
    KEY_BACKSPACE = 0x00000108,                             // Backspace key (changed from 0x00000008)
    KEY_TAB = 0x00000109,                                   // Tab key (changed from 0x00000009)
    KEY_DELETE = 0x0000002E,                                // Delete key
    KEY_INSERT = 0x0000002D,                                // Insert key
    KEY_ESCAPE = 0x0000001B,                                // Escape key

    // Lock keys
    KEY_CAPS_LOCK = 0x00000114,                             // Caps Lock key (changed from 0x00000014)
    KEY_NUM_LOCK = 0x00000090,                              // Num Lock key
    KEY_SCROLL_LOCK = 0x00000091,                           // Scroll Lock key

    // Numpad keys
    KEY_NUMPAD_0 = 0x00000060,                              // Numpad 0
    KEY_NUMPAD_1 = 0x00000061,                              // Numpad 1
    KEY_NUMPAD_2 = 0x00000062,                              // Numpad 2
    KEY_NUMPAD_3 = 0x00000063,                              // Numpad 3
    KEY_NUMPAD_4 = 0x00000064,                              // Numpad 4
    KEY_NUMPAD_5 = 0x00000065,                              // Numpad 5
    KEY_NUMPAD_6 = 0x00000066,                              // Numpad 6
    KEY_NUMPAD_7 = 0x00000067,                              // Numpad 7
    KEY_NUMPAD_8 = 0x00000068,                              // Numpad 8
    KEY_NUMPAD_9 = 0x00000069,                              // Numpad 9
    KEY_NUMPAD_MULTIPLY = 0x0000006A,                       // Numpad * (multiply)
    KEY_NUMPAD_ADD = 0x0000006B,                            // Numpad + (add)
    KEY_NUMPAD_SUBTRACT = 0x0000006D,                       // Numpad - (subtract)
    KEY_NUMPAD_DECIMAL = 0x0000006E,                        // Numpad . (decimal)
    KEY_NUMPAD_DIVIDE = 0x0000006F,                         // Numpad / (divide)
    KEY_NUMPAD_ENTER = 0x0000000E,                          // Numpad Enter key

    // Punctuation and symbol keys
    KEY_SEMICOLON = 0x000000BA,                             // ; (semicolon)
    KEY_EQUALS = 0x000000BB,                                // = (equals sign)
    KEY_COMMA = 0x000000BC,                                 // , (comma)
    KEY_MINUS = 0x000000BD,                                 // - (minus/hyphen)
    KEY_PERIOD = 0x000000BE,                                // . (period)
    KEY_SLASH = 0x000000BF,                                 // / (forward slash)
    KEY_GRAVE = 0x000000C0,                                 // ` (grave accent/backtick)
    KEY_BRACKET_LEFT = 0x000000DB,                          // [ (left bracket)
    KEY_BACKSLASH = 0x000000DC,                             // \ (backslash)
    KEY_BRACKET_RIGHT = 0x000000DD,                         // ] (right bracket)
    KEY_QUOTE = 0x000000DE,                                 // ' (apostrophe/quote)

    // Media and volume keys
    KEY_VOLUME_UP = 0x000000AF,                             // Volume Up key
    KEY_VOLUME_DOWN = 0x000000AE,                           // Volume Down key
    KEY_VOLUME_MUTE = 0x000000AD,                           // Volume Mute key
    KEY_MEDIA_PLAY_PAUSE = 0x000000B3,                      // Media Play/Pause key
    KEY_MEDIA_STOP = 0x000000B2,                            // Media Stop key
    KEY_MEDIA_PREV = 0x000000B1,                            // Media Previous Track key
    KEY_MEDIA_NEXT = 0x000000B0,                            // Media Next Track key

    // Browser and application keys
    KEY_BROWSER_BACK = 0x000000A6,                          // Browser Back key
    KEY_BROWSER_FORWARD = 0x000000A7,                       // Browser Forward key
    KEY_BROWSER_REFRESH = 0x000000A8,                       // Browser Refresh key
    KEY_BROWSER_STOP = 0x000000A9,                          // Browser Stop key
    KEY_BROWSER_SEARCH = 0x000000AA,                        // Browser Search key
    KEY_BROWSER_FAVORITES = 0x000000AB,                     // Browser Favorites key
    KEY_BROWSER_HOME = 0x000000AC,                          // Browser Home key

    // Platform-specific special keys
    KEY_PRINT_SCREEN = 0x0000002C,                          // Print Screen key
    KEY_PAUSE = 0x00000013,                                 // Pause/Break key
    KEY_MENU = 0x0000005D,                                  // Menu/Context key
    KEY_SLEEP = 0x0000005F,                                 // Sleep key

    // Android-specific keys
    KEY_ANDROID_BACK = 0x00001001,                          // Android Back button
    KEY_ANDROID_HOME = 0x00001002,                          // Android Home button
    KEY_ANDROID_MENU = 0x00001003,                          // Android Menu button
    KEY_ANDROID_SEARCH = 0x00001004,                        // Android Search button
    KEY_ANDROID_VOLUME_UP = 0x00001005,                     // Android Volume Up
    KEY_ANDROID_VOLUME_DOWN = 0x00001006,                   // Android Volume Down
    KEY_ANDROID_POWER = 0x00001007,                         // Android Power button

    // macOS-specific keys
    KEY_MACOS_COMMAND = 0x00002001,                         // macOS Command key
    KEY_MACOS_OPTION = 0x00002002,                          // macOS Option key
    KEY_MACOS_CONTROL = 0x00002003,                         // macOS Control key
    KEY_MACOS_FN = 0x00002004,                              // macOS Function key

    // Special reserved value
    KEY_UNKNOWN = 0xFFFFFFFF                                // Unknown or unmapped key
};

//==============================================================================
// Key State Structure for efficient storage
//==============================================================================
struct KeyState {
    std::atomic<bool> isPressed;                            // Current pressed state
    std::atomic<bool> wasPressed;                           // Previous frame state for edge detection
    std::chrono::steady_clock::time_point pressTime;       // When key was pressed
    std::chrono::steady_clock::time_point releaseTime;     // When key was released
    uint32_t repeatCount;                                   // Key repeat counter

    // Constructor with default initialization
    KeyState() : isPressed(false), wasPressed(false), repeatCount(0) {
        pressTime = std::chrono::steady_clock::time_point::min();
        releaseTime = std::chrono::steady_clock::time_point::min();
    }
};

//==============================================================================
// Key Log Entry for AI system integration
//==============================================================================
struct KeyLogEntry {
    KeyCode keyCode;                                        // Which key was pressed/released
    bool isKeyDown;                                         // True for press, false for release
    std::chrono::steady_clock::time_point timestamp;       // When the event occurred
    uint32_t modifierFlags;                                 // Active modifier keys (Ctrl, Shift, Alt, etc.)

    // Constructor with default initialization
    KeyLogEntry() : keyCode(KeyCode::KEY_UNKNOWN), isKeyDown(false), modifierFlags(0) {
        timestamp = std::chrono::steady_clock::now();
    }

    // Parameterized constructor for easy creation
    KeyLogEntry(KeyCode key, bool down, uint32_t modifiers = 0) :
        keyCode(key), isKeyDown(down), modifierFlags(modifiers) {
        timestamp = std::chrono::steady_clock::now();
    }
};

//==============================================================================
// Key Event Handler Function Types
//==============================================================================
using KeyDownHandler = std::function<void(KeyCode keyCode, uint32_t modifierFlags)>;
using KeyUpHandler = std::function<void(KeyCode keyCode, uint32_t modifierFlags)>;
using KeyComboHandler = std::function<void(const std::vector<KeyCode>& keys, uint32_t modifierFlags)>;

//==============================================================================
// Lock State Structure for special keys
//==============================================================================
struct LockKeyStates {
    std::atomic<bool> capsLockOn;                           // Caps Lock state
    std::atomic<bool> numLockOn;                            // Num Lock state
    std::atomic<bool> scrollLockOn;                         // Scroll Lock state

    // Constructor with default initialization
    LockKeyStates() : capsLockOn(false), numLockOn(false), scrollLockOn(false) {}
};

//==============================================================================
// Keyboard Handler Configuration
//==============================================================================
struct KeyboardConfig {
    bool enableKeyLogging;                                  // Enable key logging for AI system
    bool enableHotKeyBlocking;                              // Block OS hotkeys during application focus
    bool enableKeyRepeat;                                   // Allow key repeat events
    uint32_t keyRepeatDelay;                                // Delay before key repeat starts (ms)
    uint32_t keyRepeatRate;                                 // Key repeat rate (ms between repeats)
    bool enableMultiKeyDetection;                           // Enable multi-key combination detection
    uint32_t maxCombinationKeys;                            // Maximum keys in a combination

    // Constructor with sensible defaults for gaming
    KeyboardConfig() :
        enableKeyLogging(true),
        enableHotKeyBlocking(true),
        enableKeyRepeat(true),
        keyRepeatDelay(500),                                // 500ms delay before repeat
        keyRepeatRate(50),                                  // 50ms between repeats (20 Hz)
        enableMultiKeyDetection(true),
        maxCombinationKeys(8)                               // Support up to 8-key combinations
    {
    }
};

//==============================================================================
// Main KeyboardHandler Class Declaration (Singleton Pattern)
//==============================================================================
class KeyboardHandler {
public:
    // Singleton access method
    static KeyboardHandler& GetInstance();

    // Destructor
    ~KeyboardHandler();

    //==========================================================================
    // Initialization and Cleanup Methods
    //==========================================================================
    // Initialize the keyboard handler system with configuration
    bool Initialize(const KeyboardConfig& config = KeyboardConfig());

    // Clean up all keyboard resources and restore OS state
    void Cleanup();

    // Check if keyboard handler is properly initialized
    bool IsInitialized() const { return m_isInitialized.load(); }

    //==========================================================================
    // System Control Methods
    //==========================================================================
    // Enable keyboard system (blocks OS hotkeys, captures input)
    bool EnableKeyboardSystem();

    // Disable keyboard system (restores OS hotkeys, releases input)
    bool DisableKeyboardSystem();

    // Check if keyboard system is currently enabled
    bool IsKeyboardSystemEnabled() const { return m_isEnabled.load(); }

    //==========================================================================
    // Threading Control Methods
    //==========================================================================
    // Start keyboard processing thread
    bool StartKeyboardThread();

    // Stop keyboard processing thread gracefully
    bool StopKeyboardThread();

    // Terminate keyboard processing thread forcefully
    bool TerminateKeyboardThread();

    // Check if keyboard thread is running
    bool IsKeyboardThreadRunning() const { return m_threadRunning.load(); }

    //==========================================================================
    // Key State Query Methods (Thread-Safe, Lock-Free for Game Loop)
    //==========================================================================
    // Check if a specific key is currently pressed
    bool IsKeyPressed(KeyCode keyCode) const;

    // Check if a key was just pressed this frame (edge detection)
    bool IsKeyJustPressed(KeyCode keyCode) const;

    // Check if a key was just released this frame (edge detection)
    bool IsKeyJustReleased(KeyCode keyCode) const;

    // Get how long a key has been held down (in milliseconds)
    uint64_t GetKeyHoldDuration(KeyCode keyCode) const;

    // Check if multiple keys are pressed simultaneously
    bool AreKeysPressed(const std::vector<KeyCode>& keys) const;

    //==========================================================================
    // Lock Key State Methods
    //==========================================================================
    // Get Caps Lock state
    bool IsCapsLockOn() const { return m_lockStates.capsLockOn.load(); }

    // Get Num Lock state
    bool IsNumLockOn() const { return m_lockStates.numLockOn.load(); }

    // Get Scroll Lock state
    bool IsScrollLockOn() const { return m_lockStates.scrollLockOn.load(); }

    //==========================================================================
    // Event Handler Registration Methods
    //==========================================================================
    // Set custom key down handler
    void SetKeyDownHandler(KeyDownHandler handler);

    // Set custom key up handler
    void SetKeyUpHandler(KeyUpHandler handler);

    // Set custom key combination handler
    void SetKeyComboHandler(KeyComboHandler handler);

    // Register hotkey combination with callback
    bool RegisterHotkey(const std::vector<KeyCode>& keys, std::function<void()> callback);

    // Unregister hotkey combination
    bool UnregisterHotkey(const std::vector<KeyCode>& keys);

    //==========================================================================
    // Key Logging Methods (for AI System Integration)
    //==========================================================================
    // Get recent key log entries (last N entries)
    std::vector<KeyLogEntry> GetRecentKeyLog(uint32_t maxEntries = MAX_KEY_LOG_ENTRIES) const;

    // Clear key log history
    void ClearKeyLog();

    // Get key log statistics
    uint64_t GetTotalKeysLogged() const { return m_totalKeysLogged.load(); }

    //==========================================================================
    // Utility Methods
    //==========================================================================
    // Convert KeyCode to human-readable string
    std::string KeyCodeToString(KeyCode keyCode) const;

    // Convert platform-specific key code to our KeyCode enum
    KeyCode PlatformKeyToKeyCode(uint32_t platformKey) const;

    // Get current modifier flags (Ctrl, Shift, Alt combinations)
    uint32_t GetCurrentModifierFlags() const;

    // Update key states (called by game loop for edge detection)
    void UpdateKeyStates();

    //==========================================================================
    // Performance Monitoring Methods
    //==========================================================================
    // Get keyboard processing statistics
    void GetKeyboardStats(uint64_t& eventsProcessed, float& avgProcessingTime) const;

    // Get thread performance metrics
    bool GetThreadPerformanceMetrics(float& cpuUsage, uint64_t& memoryUsage) const;

private:
    // Private constructor for singleton pattern
    KeyboardHandler();

    //==========================================================================
    // Private Threading Methods
    //==========================================================================
    // Main keyboard processing thread function
    void KeyboardThreadFunction();

    // Process platform-specific keyboard events
    void ProcessKeyboardEvents();

    //==========================================================================
    // Private Event Processing Methods
    //==========================================================================
    // Handle key down event internally
    void HandleKeyDown(KeyCode keyCode, uint32_t modifierFlags);

    // Handle key up event internally
    void HandleKeyUp(KeyCode keyCode, uint32_t modifierFlags);

    // Process key combinations and hotkeys
    void ProcessKeyCombinations();

    // Add entry to key log for AI system
    void AddToKeyLog(KeyCode keyCode, bool isKeyDown, uint32_t modifierFlags);

    //==========================================================================
    // Private Platform-Specific Methods
    //==========================================================================
    // Initialize platform-specific keyboard hooks
    bool InitializePlatformHooks();

    // Clean up platform-specific keyboard hooks
    void CleanupPlatformHooks();

    // Save current OS hotkey states
    bool SaveOSHotkeyStates();

    // Restore OS hotkey states
    bool RestoreOSHotkeyStates();

    // Update lock key states from OS
    void UpdateLockKeyStates();

    //==========================================================================
    // Private Utility Methods
    //==========================================================================
    // Calculate hash for key combination (for hotkey storage)
    uint64_t CalculateKeyComboHash(const std::vector<KeyCode>& keys) const;

    // Validate key combination for registration
    bool ValidateKeyCombo(const std::vector<KeyCode>& keys) const;

    //==========================================================================
    // System State Variables
    //==========================================================================
    std::atomic<bool> m_isInitialized;                      // Initialization status
    std::atomic<bool> m_isEnabled;                          // System enabled status
    std::atomic<bool> m_threadRunning;                      // Thread running status
    std::atomic<bool> m_shouldShutdown;                     // Shutdown signal for thread

    //==========================================================================
    // Configuration and State Storage
    //==========================================================================
    KeyboardConfig m_config;                               // Keyboard handler configuration
    LockKeyStates m_lockStates;                            // Lock key states (Caps, Num, Scroll)

    //==========================================================================
    // Key State Management (Lock-Free for Performance)
    //==========================================================================
    std::array<KeyState, 512> m_keyStates;                 // Fast key state array (indexed by KeyCode)
    std::atomic<uint32_t> m_currentModifierFlags;          // Current modifier key flags

    //==========================================================================
    // Event Handlers (Atomic Pointers for Thread Safety)
    //==========================================================================
    std::atomic<KeyDownHandler*> m_keyDownHandler;         // Custom key down handler
    std::atomic<KeyUpHandler*> m_keyUpHandler;             // Custom key up handler
    std::atomic<KeyComboHandler*> m_keyComboHandler;       // Custom key combination handler

    //==========================================================================
    // Key Logging System (for AI Integration)
    //==========================================================================
    mutable std::mutex m_keyLogMutex;                      // Mutex for key log access
    std::queue<KeyLogEntry> m_keyLog;                      // Circular key log buffer
    std::atomic<uint64_t> m_totalKeysLogged;               // Total keys logged counter

    //==========================================================================
    // Hotkey Registration System
    //==========================================================================
    mutable std::mutex m_hotkeyMutex;                      // Mutex for hotkey map access
    std::unordered_map<uint64_t, std::function<void()>> m_registeredHotkeys; // Registered hotkey callbacks

    //==========================================================================
    // Platform-Specific State Storage
    //==========================================================================
#if defined(PLATFORM_WINDOWS)
    HHOOK m_keyboardHook;                                  // Windows keyboard hook handle
    std::vector<UINT> m_savedHotkeys;                      // Saved Windows hotkey registrations
    static LRESULT CALLBACK WindowsKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
#elif defined(PLATFORM_LINUX)
    Display* m_display;                                    // X11 display connection
    Window m_rootWindow;                                   // X11 root window for events
    bool m_savedHotkeysState;                              // Saved Linux hotkey state
#elif defined(PLATFORM_MACOS)
    CFMachPortRef m_eventTap;                              // macOS event tap for keyboard events
    CFRunLoopSourceRef m_runLoopSource;                    // macOS run loop source
    static CGEventRef MacOSKeyboardCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon);
#elif defined(PLATFORM_ANDROID)
    // Android-specific keyboard state (handled through JNI)
    bool m_androidInitialized;                            // Android keyboard initialization status
#elif defined(PLATFORM_IOS)
    // iOS-specific keyboard state (handled through UIKit)
    bool m_iosInitialized;                                 // iOS keyboard initialization status
#endif

    //==========================================================================
    // Performance Monitoring
    //==========================================================================
    std::atomic<uint64_t> m_eventsProcessed;               // Total events processed counter
    std::atomic<uint64_t> m_processingTimeTotal;           // Total processing time in microseconds
    std::chrono::steady_clock::time_point m_lastStatsUpdate; // Last statistics update time

    //==========================================================================
    // Thread Safety (Minimal Locking Design)
    //==========================================================================
    mutable std::mutex m_stateMutex;                       // Mutex for state changes only
    std::condition_variable m_threadStopCV;                // Condition variable for thread stopping

    // Prevent copying of singleton
    KeyboardHandler(const KeyboardHandler&) = delete;
    KeyboardHandler& operator=(const KeyboardHandler&) = delete;
};

#pragma warning(pop)