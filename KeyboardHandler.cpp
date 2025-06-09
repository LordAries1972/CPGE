// -------------------------------------------------------------------------------------------------------------
// KeyboardHandler.cpp - Cross-Platform Singleton Keyboard Handler System Implementation
// Section 1: Basic Structure, Constructor, Destructor and Core Initialization
// -------------------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "KeyboardHandler.h"
#include "ThreadLockHelper.h"

// External references required by the KeyboardHandler system
extern Debug debug;
extern ThreadManager threadManager;

// Suppress specific warnings for optimized cross-platform code
#pragma warning(push)
#pragma warning(disable: 4996)  // Suppress deprecated function warnings for cross-platform compatibility
#pragma warning(disable: 4127)  // Suppress conditional expression is constant for template optimizations

//==============================================================================
// Global Static Instance and Platform-Specific Variables
//==============================================================================
static KeyboardHandler* g_keyboardHandlerInstance = nullptr;
static std::mutex g_instanceMutex;

// Platform-specific global variables for callbacks
#if defined(PLATFORM_WINDOWS)
    static KeyboardHandler* g_windowsKeyboardInstance = nullptr;
#elif defined(PLATFORM_MACOS)
    static KeyboardHandler* g_macosKeyboardInstance = nullptr;
#endif

//==============================================================================
// Constructor and Destructor Implementation
//==============================================================================

// Private constructor - Initialize KeyboardHandler system with default values
KeyboardHandler::KeyboardHandler() :
    m_isInitialized(false),                                 // System not initialized by default
    m_isEnabled(false),                                     // System not enabled by default
    m_threadRunning(false),                                 // Thread not running by default
    m_shouldShutdown(false),                                // No shutdown requested initially
    m_currentModifierFlags(0),                              // No modifier keys pressed initially
    m_keyDownHandler(nullptr),                              // No custom key down handler initially
    m_keyUpHandler(nullptr),                                // No custom key up handler initially
    m_keyComboHandler(nullptr),                             // No custom key combo handler initially
    m_totalKeysLogged(0),                                   // No keys logged initially
    m_eventsProcessed(0),                                   // No events processed initially
    m_processingTimeTotal(0),                               // No processing time accumulated
    m_lastStatsUpdate(std::chrono::steady_clock::now())     // Set current time as last stats update
{
    #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler constructor called - initializing singleton keyboard system");
    #endif

// Initialize all key states to default (not pressed)
   for (auto& keyState : m_keyStates) {
       keyState.isPressed.store(false);                    // Key not pressed
       keyState.wasPressed.store(false);                   // Key was not pressed in previous frame
       keyState.repeatCount = 0;                           // No repeat count
       keyState.pressTime = std::chrono::steady_clock::time_point::min(); // Invalid press time
       keyState.releaseTime = std::chrono::steady_clock::time_point::min(); // Invalid release time
   }

   // Initialize platform-specific members
   #if defined(PLATFORM_WINDOWS)
       m_keyboardHook = nullptr;                           // No keyboard hook installed initially
       m_savedHotkeys.clear();                             // Clear saved hotkeys vector
       g_windowsKeyboardInstance = this;                   // Set global instance for callback
   #elif defined(PLATFORM_LINUX)
       m_display = nullptr;                                // No X11 display connection initially
       m_rootWindow = 0;                                   // No root window initially
       m_savedHotkeysState = false;                        // No saved hotkey state initially
   #elif defined(PLATFORM_MACOS)
       m_eventTap = nullptr;                               // No event tap initially
       m_runLoopSource = nullptr;                          // No run loop source initially
       g_macosKeyboardInstance = this;                     // Set global instance for callback
   #elif defined(PLATFORM_ANDROID)
       m_androidInitialized = false;                      // Android not initialized initially
   #elif defined(PLATFORM_IOS)
       m_iosInitialized = false;                          // iOS not initialized initially
   #endif

   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler constructor completed - system ready for initialization");
   #endif
}

// Destructor - Clean up KeyboardHandler system resources
KeyboardHandler::~KeyboardHandler() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler destructor called - cleaning up keyboard system");
   #endif

   // Ensure proper cleanup
   Cleanup();

   // Clear global instance pointer
   #if defined(PLATFORM_WINDOWS)
       g_windowsKeyboardInstance = nullptr;                // Clear global Windows instance
   #elif defined(PLATFORM_MACOS)
       g_macosKeyboardInstance = nullptr;                  // Clear global macOS instance
   #endif

   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler destructor completed - all resources cleaned up");
   #endif
}

//==============================================================================
// Singleton Access Method
//==============================================================================

// Get the singleton instance of KeyboardHandler (thread-safe)
KeyboardHandler& KeyboardHandler::GetInstance() {
   // Use double-checked locking for optimal performance
   if (g_keyboardHandlerInstance == nullptr) {
       std::lock_guard<std::mutex> lock(g_instanceMutex);
       if (g_keyboardHandlerInstance == nullptr) {
           g_keyboardHandlerInstance = new KeyboardHandler();

           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler singleton instance created");
           #endif
       }
   }
   return *g_keyboardHandlerInstance;
}

//==============================================================================
// Core Initialization and Cleanup Methods
//==============================================================================

// Initialize the keyboard handler system with specified configuration
bool KeyboardHandler::Initialize(const KeyboardConfig& config) {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler::Initialize() called - starting keyboard system initialization");
   #endif

   // Prevent double initialization
   if (m_isInitialized.load()) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_WARNING, L"KeyboardHandler already initialized - skipping");
       #endif
       return true;                                        // Already initialized
   }

   // Use ThreadLockHelper for thread-safe initialization
   ThreadLockHelper initLock(threadManager, "keyboard_init", 5000);
   if (!initLock.IsLocked()) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire initialization lock - cannot initialize KeyboardHandler");
       #endif
       return false;                                       // Failed to acquire lock
   }

   try {
       // Store configuration settings
       m_config = config;                                  // Save keyboard configuration

       // Validate configuration parameters
       if (m_config.keyRepeatDelay < 50) {                 // Minimum 50ms repeat delay
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_WARNING, L"Key repeat delay too short, setting to minimum 50ms");
           #endif
           m_config.keyRepeatDelay = 50;                   // Set minimum delay
       }

       if (m_config.keyRepeatRate < 10) {                  // Minimum 10ms repeat rate
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_WARNING, L"Key repeat rate too fast, setting to minimum 10ms");
           #endif
           m_config.keyRepeatRate = 10;                    // Set minimum rate
       }

       if (m_config.maxCombinationKeys > 16) {             // Maximum 16 keys in combination
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_WARNING, L"Max combination keys too high, setting to maximum 16");
           #endif
           m_config.maxCombinationKeys = 16;               // Set maximum combination size
       }

       // Initialize platform-specific keyboard hooks
       if (!InitializePlatformHooks()) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize platform-specific keyboard hooks");
           #endif
           return false;                                   // Failed to initialize platform hooks
       }

       // Update lock key states from OS
       UpdateLockKeyStates();

       // Set default event handlers
       SetKeyDownHandler([this](KeyCode keyCode, uint32_t modifierFlags) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Default KeyDown handler: Key 0x%08X, Modifiers 0x%08X", 
                   static_cast<uint32_t>(keyCode), modifierFlags);
           #endif
       });

       SetKeyUpHandler([this](KeyCode keyCode, uint32_t modifierFlags) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Default KeyUp handler: Key 0x%08X, Modifiers 0x%08X", 
                   static_cast<uint32_t>(keyCode), modifierFlags);
           #endif
       });

       // Mark system as successfully initialized
       m_isInitialized.store(true);                        // Set initialization flag

       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_INFO, 
               L"KeyboardHandler initialization completed successfully - Logging: %s, Hotkey blocking: %s", 
               m_config.enableKeyLogging ? L"Enabled" : L"Disabled",
               m_config.enableHotKeyBlocking ? L"Enabled" : L"Disabled");
       #endif

       return true;                                        // Initialization successful
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during KeyboardHandler initialization: %S", e.what());
       #endif
       return false;                                       // Initialization failed
   }
}

// Clean up all keyboard resources and save current state
void KeyboardHandler::Cleanup() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler::Cleanup() called - cleaning up keyboard system");
   #endif

   try {
       // Signal shutdown to all threads
       m_shouldShutdown.store(true);                       // Set shutdown flag

       // Stop keyboard processing thread if running
       if (m_threadRunning.load()) {
           StopKeyboardThread();                           // Stop thread gracefully
       }

       // Disable keyboard system to restore OS state
       if (m_isEnabled.load()) {
           DisableKeyboardSystem();                        // Restore OS hotkeys and input handling
       }

       // Clean up platform-specific resources
       CleanupPlatformHooks();

       // Clear event handlers
       m_keyDownHandler.store(nullptr);                    // Clear key down handler
       m_keyUpHandler.store(nullptr);                      // Clear key up handler
       m_keyComboHandler.store(nullptr);                   // Clear key combo handler

       // Clear registered hotkeys
       {
           ThreadLockHelper hotkeyLock(threadManager, "keyboard_hotkey_cleanup", 2000);
           if (hotkeyLock.IsLocked()) {
               m_registeredHotkeys.clear();                // Clear all registered hotkeys
           }
       }

       // Clear key log
       ClearKeyLog();

       // Reset all key states
       for (auto& keyState : m_keyStates) {
           keyState.isPressed.store(false);                // Reset pressed state
           keyState.wasPressed.store(false);               // Reset previous state
           keyState.repeatCount = 0;                       // Reset repeat count
       }

       // Reset state flags
       m_isInitialized.store(false);                       // Clear initialization flag
       m_currentModifierFlags.store(0);                    // Clear modifier flags

       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler cleanup completed successfully");
       #endif
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during KeyboardHandler cleanup: %S", e.what());
       #endif
   }
}

//==============================================================================
// System Control Methods Implementation
//==============================================================================

// Enable keyboard system (blocks OS hotkeys, captures input)
bool KeyboardHandler::EnableKeyboardSystem() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler::EnableKeyboardSystem() called");
   #endif

   // Check if system is initialized
   if (!m_isInitialized.load()) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot enable keyboard system - not initialized");
       #endif
       return false;                                       // System not initialized
   }

   // Check if already enabled
   if (m_isEnabled.load()) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_WARNING, L"Keyboard system already enabled");
       #endif
       return true;                                        // Already enabled
   }

   try {
       // Save current OS hotkey states before blocking
       if (m_config.enableHotKeyBlocking) {
           if (!SaveOSHotkeyStates()) {
               #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                   debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to save OS hotkey states - continuing without blocking");
               #endif
           }
       }

       // Start keyboard processing thread
       if (!StartKeyboardThread()) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to start keyboard processing thread");
           #endif
           return false;                                   // Failed to start thread
       }

       // Mark system as enabled
       m_isEnabled.store(true);                            // Set enabled flag

       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard system enabled successfully");
       #endif

       return true;                                        // System enabled successfully
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception enabling keyboard system: %S", e.what());
       #endif
       return false;                                       // Failed to enable system
   }
}

// Disable keyboard system (restores OS hotkeys, releases input)
bool KeyboardHandler::DisableKeyboardSystem() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler::DisableKeyboardSystem() called");
   #endif

   // Check if currently enabled
   if (!m_isEnabled.load()) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_WARNING, L"Keyboard system not enabled - nothing to disable");
       #endif
       return true;                                        // Not enabled, nothing to disable
   }

   try {
       // Stop keyboard processing thread
       if (m_threadRunning.load()) {
           StopKeyboardThread();                           // Stop thread gracefully
       }

       // Restore OS hotkey states if they were blocked
       if (m_config.enableHotKeyBlocking) {
           if (!RestoreOSHotkeyStates()) {
               #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                   debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to restore OS hotkey states");
               #endif
           }
       }

       // Mark system as disabled
       m_isEnabled.store(false);                           // Clear enabled flag

       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard system disabled successfully");
       #endif

       return true;                                        // System disabled successfully
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception disabling keyboard system: %S", e.what());
       #endif
       return false;                                       // Failed to disable system
   }
}

//==============================================================================
// Threading Control Methods Implementation
//==============================================================================

// Start keyboard processing thread
bool KeyboardHandler::StartKeyboardThread() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler::StartKeyboardThread() called");
   #endif

   // Check if thread is already running
   if (m_threadRunning.load()) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_WARNING, L"Keyboard thread already running");
       #endif
       return true;                                        // Already running
   }

   try {
       // Reset shutdown flag
       m_shouldShutdown.store(false);                      // Clear shutdown signal

       // Create and start keyboard processing thread using ThreadManager
       threadManager.SetThread(THREAD_AI_PROCESSING, [this]() {
           KeyboardThreadFunction();                       // Main keyboard thread function
       }, true);                                           // Enable debug mode for keyboard thread

       // Mark thread as running
       m_threadRunning.store(true);                        // Set thread running flag

       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard processing thread started successfully");
       #endif

       return true;                                        // Thread started successfully
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception starting keyboard thread: %S", e.what());
       #endif
       return false;                                       // Failed to start thread
   }
}

// Stop keyboard processing thread gracefully
bool KeyboardHandler::StopKeyboardThread() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"KeyboardHandler::StopKeyboardThread() called");
   #endif

   // Check if thread is running
   if (!m_threadRunning.load()) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_WARNING, L"Keyboard thread not running - nothing to stop");
       #endif
       return true;                                        // Not running, nothing to stop
   }

   try {
       // Signal thread to shutdown
       m_shouldShutdown.store(true);                       // Set shutdown signal

       // Notify thread to wake up if waiting
       m_threadStopCV.notify_all();                        // Wake up waiting thread

       // Stop keyboard thread using ThreadManager
       if (threadManager.DoesThreadExist(THREAD_AI_PROCESSING)) {
           threadManager.StopThread(THREAD_AI_PROCESSING); // Stop thread gracefully
       }

       // Mark thread as not running
       m_threadRunning.store(false);                       // Clear thread running flag

       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard processing thread stopped successfully");
       #endif

       return true;                                        // Thread stopped successfully
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception stopping keyboard thread: %S", e.what());
       #endif
       return false;                                       // Failed to stop thread
   }
}

// Terminate keyboard processing thread forcefully
bool KeyboardHandler::TerminateKeyboardThread() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_WARNING, L"KeyboardHandler::TerminateKeyboardThread() called - forceful termination");
   #endif

   try {
       // Set shutdown flag
       m_shouldShutdown.store(true);                       // Force shutdown signal

       // Terminate keyboard thread using ThreadManager
       if (threadManager.DoesThreadExist(THREAD_AI_PROCESSING)) {
           threadManager.TerminateThread(THREAD_AI_PROCESSING); // Terminate thread forcefully
       }

       // Mark thread as not running
       m_threadRunning.store(false);                       // Clear thread running flag

       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_WARNING, L"Keyboard processing thread terminated forcefully");
       #endif

       return true;                                        // Thread terminated
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception terminating keyboard thread: %S", e.what());
       #endif
       return false;                                       // Failed to terminate thread
   }
}

//==============================================================================
// Key State Query Methods Implementation (Lock-Free for Game Loop Performance)
//==============================================================================

// Check if a specific key is currently pressed
bool KeyboardHandler::IsKeyPressed(KeyCode keyCode) const {
   // Convert KeyCode to array index for fast access
   uint32_t keyIndex = static_cast<uint32_t>(keyCode) % m_keyStates.size();
   
   // Return current pressed state (atomic read, no locking needed)
   return m_keyStates[keyIndex].isPressed.load();
}

// Check if a key was just pressed this frame (edge detection)
bool KeyboardHandler::IsKeyJustPressed(KeyCode keyCode) const {
   // Convert KeyCode to array index for fast access
   uint32_t keyIndex = static_cast<uint32_t>(keyCode) % m_keyStates.size();
   
   // Check if key is currently pressed but wasn't pressed in previous frame
   bool currentlyPressed = m_keyStates[keyIndex].isPressed.load();
   bool previouslyPressed = m_keyStates[keyIndex].wasPressed.load();
   
   return currentlyPressed && !previouslyPressed;
}

// Check if a key was just released this frame (edge detection)
bool KeyboardHandler::IsKeyJustReleased(KeyCode keyCode) const {
   // Convert KeyCode to array index for fast access
   uint32_t keyIndex = static_cast<uint32_t>(keyCode) % m_keyStates.size();
   
   // Check if key is not currently pressed but was pressed in previous frame
   bool currentlyPressed = m_keyStates[keyIndex].isPressed.load();
   bool previouslyPressed = m_keyStates[keyIndex].wasPressed.load();
   
   return !currentlyPressed && previouslyPressed;
}

// Get how long a key has been held down (in milliseconds)
uint64_t KeyboardHandler::GetKeyHoldDuration(KeyCode keyCode) const {
   // Convert KeyCode to array index for fast access
   uint32_t keyIndex = static_cast<uint32_t>(keyCode) % m_keyStates.size();
   
   // Check if key is currently pressed
   if (!m_keyStates[keyIndex].isPressed.load()) {
       return 0;                                           // Key not pressed, no hold duration
   }
   
   // Calculate duration since key was pressed
   auto currentTime = std::chrono::steady_clock::now();
   auto pressTime = m_keyStates[keyIndex].pressTime;
   
   // Validate press time
   if (pressTime == std::chrono::steady_clock::time_point::min()) {
       return 0;                                           // Invalid press time
   }
   
   // Calculate and return duration in milliseconds
   auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - pressTime);
   return static_cast<uint64_t>(duration.count());
}

// Check if multiple keys are pressed simultaneously
bool KeyboardHandler::AreKeysPressed(const std::vector<KeyCode>& keys) const {
   // Check if all specified keys are currently pressed
   for (KeyCode keyCode : keys) {
       if (!IsKeyPressed(keyCode)) {
           return false;                                   // At least one key is not pressed
       }
   }
   
   return true;                                            // All keys are pressed
}

//==============================================================================
// Event Handler Registration Methods Implementation
//==============================================================================

// Set custom key down handler
void KeyboardHandler::SetKeyDownHandler(KeyDownHandler handler) {
   // Store handler pointer atomically (thread-safe)
   KeyDownHandler* handlerPtr = new KeyDownHandler(std::move(handler));
   
   // Get previous handler and replace atomically
   KeyDownHandler* oldHandler = m_keyDownHandler.exchange(handlerPtr);
   
   // Clean up previous handler
   delete oldHandler;
   
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Custom key down handler registered");
   #endif
}

// Set custom key up handler
void KeyboardHandler::SetKeyUpHandler(KeyUpHandler handler) {
   // Store handler pointer atomically (thread-safe)
   KeyUpHandler* handlerPtr = new KeyUpHandler(std::move(handler));
   
   // Get previous handler and replace atomically
   KeyUpHandler* oldHandler = m_keyUpHandler.exchange(handlerPtr);
   
   // Clean up previous handler
   delete oldHandler;
   
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Custom key up handler registered");
   #endif
}

// Set custom key combination handler
void KeyboardHandler::SetKeyComboHandler(KeyComboHandler handler) {
   // Store handler pointer atomically (thread-safe)
   KeyComboHandler* handlerPtr = new KeyComboHandler(std::move(handler));
   
   // Get previous handler and replace atomically
   KeyComboHandler* oldHandler = m_keyComboHandler.exchange(handlerPtr);
   
   // Clean up previous handler
   delete oldHandler;
   
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Custom key combination handler registered");
   #endif
}

// Register hotkey combination with callback
bool KeyboardHandler::RegisterHotkey(const std::vector<KeyCode>& keys, std::function<void()> callback) {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Registering hotkey combination with %zu keys", keys.size());
   #endif
   
   // Validate key combination
   if (!ValidateKeyCombo(keys)) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid key combination for hotkey registration");
       #endif
       return false;                                       // Invalid combination
   }
   
   try {
       // Calculate hash for key combination
       uint64_t comboHash = CalculateKeyComboHash(keys);
       
       // Register hotkey with thread safety
       ThreadLockHelper hotkeyLock(threadManager, "keyboard_hotkey_register", 2000);
       if (!hotkeyLock.IsLocked()) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire hotkey lock for registration");
           #endif
           return false;                                   // Failed to acquire lock
       }
       
       // Store hotkey callback
       m_registeredHotkeys[comboHash] = std::move(callback);
       
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_INFO, L"Hotkey registered successfully - Hash: 0x%016llX", comboHash);
       #endif
       
       return true;                                        // Hotkey registered successfully
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception registering hotkey: %S", e.what());
       #endif
       return false;                                       // Failed to register hotkey
   }
}

// Unregister hotkey combination
bool KeyboardHandler::UnregisterHotkey(const std::vector<KeyCode>& keys) {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Unregistering hotkey combination with %zu keys", keys.size());
   #endif
   
   try {
       // Calculate hash for key combination
       uint64_t comboHash = CalculateKeyComboHash(keys);
       
       // Unregister hotkey with thread safety
       ThreadLockHelper hotkeyLock(threadManager, "keyboard_hotkey_unregister", 2000);
       if (!hotkeyLock.IsLocked()) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire hotkey lock for unregistration");
           #endif
           return false;                                   // Failed to acquire lock
       }
       
       // Remove hotkey from map
       auto it = m_registeredHotkeys.find(comboHash);
       if (it != m_registeredHotkeys.end()) {
           m_registeredHotkeys.erase(it);
           
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logDebugMessage(LogLevel::LOG_INFO, L"Hotkey unregistered successfully - Hash: 0x%016llX", comboHash);
           #endif
           
           return true;                                    // Hotkey unregistered successfully
       }
       else {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logDebugMessage(LogLevel::LOG_WARNING, L"Hotkey not found for unregistration - Hash: 0x%016llX", comboHash);
           #endif
           return false;                                   // Hotkey not found
       }
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception unregistering hotkey: %S", e.what());
       #endif
       return false;                                       // Failed to unregister hotkey
   }
}

//==============================================================================
// Key Logging Methods Implementation (for AI System Integration)
//==============================================================================

// Get recent key log entries (last N entries)
std::vector<KeyLogEntry> KeyboardHandler::GetRecentKeyLog(uint32_t maxEntries) const {
   std::vector<KeyLogEntry> result;
   
   // Check if key logging is enabled
   if (!m_config.enableKeyLogging) {
       return result;                                      // Return empty vector if logging disabled
   }
   
   try {
       // Access key log with thread safety
       ThreadLockHelper logLock(threadManager, "keyboard_keylog_read", 1000, true); // Silent lock
       if (!logLock.IsLocked()) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire key log lock - returning empty log");
           #endif
           return result;                                  // Return empty result on lock failure
       }
       
       // Reserve space for result vector
       uint32_t entriesToCopy = std::min(maxEntries, static_cast<uint32_t>(m_keyLog.size()));
       result.reserve(entriesToCopy);
       
       // Copy entries from circular buffer (most recent first)
       auto tempQueue = m_keyLog;                          // Create copy for iteration
       std::vector<KeyLogEntry> tempVector;
       
       // Convert queue to vector for easier access
       while (!tempQueue.empty()) {
           tempVector.push_back(tempQueue.front());
           tempQueue.pop();
       }
       
       // Copy most recent entries
       uint32_t startIndex = (tempVector.size() > entriesToCopy) ? 
           (tempVector.size() - entriesToCopy) : 0;
           
       for (uint32_t i = startIndex; i < tempVector.size(); ++i) {
           result.push_back(tempVector[i]);
       }
       
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Retrieved %zu key log entries", result.size());
       #endif
       
       return result;                                      // Return key log entries
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception getting key log: %S", e.what());
       #endif
return result;                                      // Return empty result on exception
   }
}

// Clear key log history
void KeyboardHandler::ClearKeyLog() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_DEBUG, L"KeyboardHandler::ClearKeyLog() called - clearing key log history");
   #endif
   
   try {
       // Access key log with thread safety
       ThreadLockHelper logLock(threadManager, "keyboard_keylog_clear", 2000);
       if (!logLock.IsLocked()) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire key log lock for clearing");
           #endif
           return;                                         // Failed to acquire lock
       }
       
       // Clear the key log queue
       std::queue<KeyLogEntry> emptyQueue;
       m_keyLog = std::move(emptyQueue);                   // Replace with empty queue
       
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Key log cleared successfully");
       #endif
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception clearing key log: %S", e.what());
       #endif
   }
}

//==============================================================================
// Utility Methods Implementation
//==============================================================================

// Convert KeyCode to human-readable string
std::string KeyboardHandler::KeyCodeToString(KeyCode keyCode) const {
   // Map KeyCode enum values to human-readable strings
   switch (keyCode) {
       // Alphabet keys
       case KeyCode::KEY_A: return "A";
       case KeyCode::KEY_B: return "B";
       case KeyCode::KEY_C: return "C";
       case KeyCode::KEY_D: return "D";
       case KeyCode::KEY_E: return "E";
       case KeyCode::KEY_F: return "F";
       case KeyCode::KEY_G: return "G";
       case KeyCode::KEY_H: return "H";
       case KeyCode::KEY_I: return "I";
       case KeyCode::KEY_J: return "J";
       case KeyCode::KEY_K: return "K";
       case KeyCode::KEY_L: return "L";
       case KeyCode::KEY_M: return "M";
       case KeyCode::KEY_N: return "N";
       case KeyCode::KEY_O: return "O";
       case KeyCode::KEY_P: return "P";
       case KeyCode::KEY_Q: return "Q";
       case KeyCode::KEY_R: return "R";
       case KeyCode::KEY_S: return "S";
       case KeyCode::KEY_T: return "T";
       case KeyCode::KEY_U: return "U";
       case KeyCode::KEY_V: return "V";
       case KeyCode::KEY_W: return "W";
       case KeyCode::KEY_X: return "X";
       case KeyCode::KEY_Y: return "Y";
       case KeyCode::KEY_Z: return "Z";
       
       // Number keys
       case KeyCode::KEY_0: return "0";
       case KeyCode::KEY_1: return "1";
       case KeyCode::KEY_2: return "2";
       case KeyCode::KEY_3: return "3";
       case KeyCode::KEY_4: return "4";
       case KeyCode::KEY_5: return "5";
       case KeyCode::KEY_6: return "6";
       case KeyCode::KEY_7: return "7";
       case KeyCode::KEY_8: return "8";
       case KeyCode::KEY_9: return "9";
       
       // Function keys
       case KeyCode::KEY_F1: return "F1";
       case KeyCode::KEY_F2: return "F2";
       case KeyCode::KEY_F3: return "F3";
       case KeyCode::KEY_F4: return "F4";
       case KeyCode::KEY_F5: return "F5";
       case KeyCode::KEY_F6: return "F6";
       case KeyCode::KEY_F7: return "F7";
       case KeyCode::KEY_F8: return "F8";
       case KeyCode::KEY_F9: return "F9";
       case KeyCode::KEY_F10: return "F10";
       case KeyCode::KEY_F11: return "F11";
       case KeyCode::KEY_F12: return "F12";
       
       // Modifier keys
       case KeyCode::KEY_SHIFT_LEFT: return "Left Shift";
       case KeyCode::KEY_SHIFT_RIGHT: return "Right Shift";
       case KeyCode::KEY_CTRL_LEFT: return "Left Ctrl";
       case KeyCode::KEY_CTRL_RIGHT: return "Right Ctrl";
       case KeyCode::KEY_ALT_LEFT: return "Left Alt";
       case KeyCode::KEY_ALT_RIGHT: return "Right Alt";
       case KeyCode::KEY_WIN_LEFT: return "Left Win";
       case KeyCode::KEY_WIN_RIGHT: return "Right Win";
       
       // Navigation keys
       case KeyCode::KEY_ARROW_UP: return "Up Arrow";
       case KeyCode::KEY_ARROW_DOWN: return "Down Arrow";
       case KeyCode::KEY_ARROW_LEFT: return "Left Arrow";
       case KeyCode::KEY_ARROW_RIGHT: return "Right Arrow";
       case KeyCode::KEY_HOME: return "Home";
       case KeyCode::KEY_END: return "End";
       case KeyCode::KEY_PAGE_UP: return "Page Up";
       case KeyCode::KEY_PAGE_DOWN: return "Page Down";
       
       // Special keys
       case KeyCode::KEY_SPACE: return "Space";
       case KeyCode::KEY_ENTER: return "Enter";
       case KeyCode::KEY_BACKSPACE: return "Backspace";
       case KeyCode::KEY_TAB: return "Tab";
       case KeyCode::KEY_DELETE: return "Delete";
       case KeyCode::KEY_INSERT: return "Insert";
       case KeyCode::KEY_ESCAPE: return "Escape";
       
       // Lock keys
       case KeyCode::KEY_CAPS_LOCK: return "Caps Lock";
       case KeyCode::KEY_NUM_LOCK: return "Num Lock";
       case KeyCode::KEY_SCROLL_LOCK: return "Scroll Lock";
       
       // Default case for unknown keys
       default: 
       {
           char buffer[32];
           snprintf(buffer, sizeof(buffer), "Key_0x%08X", static_cast<uint32_t>(keyCode));
           return std::string(buffer);
       }
   }
}

// Convert platform-specific key code to our KeyCode enum
KeyCode KeyboardHandler::PlatformKeyToKeyCode(uint32_t platformKey) const {
    #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[KeyboardHandler] Converting platform key 0x%08X to KeyCode", platformKey);
    #endif

    #if defined(PLATFORM_WINDOWS)
        // Windows virtual key code mapping - COMPLETE IMPLEMENTATION
        switch (platformKey) {
            // Alphabet keys (Windows VK codes match ASCII for A-Z)
            case 'A': return KeyCode::KEY_A;                            // Letter A key
            case 'B': return KeyCode::KEY_B;                            // Letter B key
            case 'C': return KeyCode::KEY_C;                            // Letter C key
            case 'D': return KeyCode::KEY_D;                            // Letter D key
            case 'E': return KeyCode::KEY_E;                            // Letter E key
            case 'F': return KeyCode::KEY_F;                            // Letter F key
            case 'G': return KeyCode::KEY_G;                            // Letter G key
            case 'H': return KeyCode::KEY_H;                            // Letter H key
            case 'I': return KeyCode::KEY_I;                            // Letter I key
            case 'J': return KeyCode::KEY_J;                            // Letter J key
            case 'K': return KeyCode::KEY_K;                            // Letter K key
            case 'L': return KeyCode::KEY_L;                            // Letter L key
            case 'M': return KeyCode::KEY_M;                            // Letter M key
            case 'N': return KeyCode::KEY_N;                            // Letter N key
            case 'O': return KeyCode::KEY_O;                            // Letter O key
            case 'P': return KeyCode::KEY_P;                            // Letter P key
            case 'Q': return KeyCode::KEY_Q;                            // Letter Q key
            case 'R': return KeyCode::KEY_R;                            // Letter R key
            case 'S': return KeyCode::KEY_S;                            // Letter S key
            case 'T': return KeyCode::KEY_T;                            // Letter T key
            case 'U': return KeyCode::KEY_U;                            // Letter U key
            case 'V': return KeyCode::KEY_V;                            // Letter V key
            case 'W': return KeyCode::KEY_W;                            // Letter W key
            case 'X': return KeyCode::KEY_X;                            // Letter X key
            case 'Y': return KeyCode::KEY_Y;                            // Letter Y key
            case 'Z': return KeyCode::KEY_Z;                            // Letter Z key

            // Number keys (top row - Windows VK codes match ASCII for 0-9)
            case '0': return KeyCode::KEY_0;                            // Number 0 key (top row)
            case '1': return KeyCode::KEY_1;                            // Number 1 key (top row)
            case '2': return KeyCode::KEY_2;                            // Number 2 key (top row)
            case '3': return KeyCode::KEY_3;                            // Number 3 key (top row)
            case '4': return KeyCode::KEY_4;                            // Number 4 key (top row)
            case '5': return KeyCode::KEY_5;                            // Number 5 key (top row)
            case '6': return KeyCode::KEY_6;                            // Number 6 key (top row)
            case '7': return KeyCode::KEY_7;                            // Number 7 key (top row)
            case '8': return KeyCode::KEY_8;                            // Number 8 key (top row)
            case '9': return KeyCode::KEY_9;                            // Number 9 key (top row)

            // Function keys (F1-F15)
            case VK_F1: return KeyCode::KEY_F1;                         // Function key F1
            case VK_F2: return KeyCode::KEY_F2;                         // Function key F2
            case VK_F3: return KeyCode::KEY_F3;                         // Function key F3
            case VK_F4: return KeyCode::KEY_F4;                         // Function key F4
            case VK_F5: return KeyCode::KEY_F5;                         // Function key F5
            case VK_F6: return KeyCode::KEY_F6;                         // Function key F6
            case VK_F7: return KeyCode::KEY_F7;                         // Function key F7
            case VK_F8: return KeyCode::KEY_F8;                         // Function key F8
            case VK_F9: return KeyCode::KEY_F9;                         // Function key F9
            case VK_F10: return KeyCode::KEY_F10;                       // Function key F10
            case VK_F11: return KeyCode::KEY_F11;                       // Function key F11
            case VK_F12: return KeyCode::KEY_F12;                       // Function key F12
            case VK_F13: return KeyCode::KEY_F13;                       // Function key F13 (extended)
            case VK_F14: return KeyCode::KEY_F14;                       // Function key F14 (extended)
            case VK_F15: return KeyCode::KEY_F15;                       // Function key F15 (extended)

            // Modifier keys (left and right variants)
            case VK_LSHIFT: return KeyCode::KEY_SHIFT_LEFT;             // Left Shift key
            case VK_RSHIFT: return KeyCode::KEY_SHIFT_RIGHT;            // Right Shift key
            case VK_LCONTROL: return KeyCode::KEY_CTRL_LEFT;            // Left Control key
            case VK_RCONTROL: return KeyCode::KEY_CTRL_RIGHT;           // Right Control key
            case VK_LMENU: return KeyCode::KEY_ALT_LEFT;                // Left Alt key
            case VK_RMENU: return KeyCode::KEY_ALT_RIGHT;               // Right Alt key (AltGr)
            case VK_LWIN: return KeyCode::KEY_WIN_LEFT;                 // Left Windows/Command key
            case VK_RWIN: return KeyCode::KEY_WIN_RIGHT;                // Right Windows/Command key

            // Navigation arrow keys
            case VK_UP: return KeyCode::KEY_ARROW_UP;                   // Up arrow key
            case VK_DOWN: return KeyCode::KEY_ARROW_DOWN;               // Down arrow key
            case VK_LEFT: return KeyCode::KEY_ARROW_LEFT;               // Left arrow key
            case VK_RIGHT: return KeyCode::KEY_ARROW_RIGHT;             // Right arrow key

            // Navigation cluster keys
            case VK_HOME: return KeyCode::KEY_HOME;                     // Home key
            case VK_END: return KeyCode::KEY_END;                       // End key
            case VK_PRIOR: return KeyCode::KEY_PAGE_UP;                 // Page Up key (VK_PRIOR)
            case VK_NEXT: return KeyCode::KEY_PAGE_DOWN;                // Page Down key (VK_NEXT)

            // Special control keys
            case VK_SPACE: return KeyCode::KEY_SPACE;                   // Space bar
            case VK_RETURN: return KeyCode::KEY_ENTER;                  // Enter/Return key
            case VK_BACK: return KeyCode::KEY_BACKSPACE;                // Backspace key
            case VK_TAB: return KeyCode::KEY_TAB;                       // Tab key
            case VK_DELETE: return KeyCode::KEY_DELETE;                 // Delete key
            case VK_INSERT: return KeyCode::KEY_INSERT;                 // Insert key
            case VK_ESCAPE: return KeyCode::KEY_ESCAPE;                 // Escape key

            // Lock state keys
            case VK_CAPITAL: return KeyCode::KEY_CAPS_LOCK;             // Caps Lock key
            case VK_NUMLOCK: return KeyCode::KEY_NUM_LOCK;              // Num Lock key
            case VK_SCROLL: return KeyCode::KEY_SCROLL_LOCK;            // Scroll Lock key

            // NUMPAD KEYS
            case VK_NUMPAD0: return KeyCode::KEY_NUMPAD_0;              // Numpad 0
            case VK_NUMPAD1: return KeyCode::KEY_NUMPAD_1;              // Numpad 1
            case VK_NUMPAD2: return KeyCode::KEY_NUMPAD_2;              // Numpad 2
            case VK_NUMPAD3: return KeyCode::KEY_NUMPAD_3;              // Numpad 3
            case VK_NUMPAD4: return KeyCode::KEY_NUMPAD_4;              // Numpad 4
            case VK_NUMPAD5: return KeyCode::KEY_NUMPAD_5;              // Numpad 5
            case VK_NUMPAD6: return KeyCode::KEY_NUMPAD_6;              // Numpad 6
            case VK_NUMPAD7: return KeyCode::KEY_NUMPAD_7;              // Numpad 7
            case VK_NUMPAD8: return KeyCode::KEY_NUMPAD_8;              // Numpad 8
            case VK_NUMPAD9: return KeyCode::KEY_NUMPAD_9;              // Numpad 9
            case VK_MULTIPLY: return KeyCode::KEY_NUMPAD_MULTIPLY;      // Numpad * (multiply)
            case VK_ADD: return KeyCode::KEY_NUMPAD_ADD;                // Numpad + (add)
            case VK_SUBTRACT: return KeyCode::KEY_NUMPAD_SUBTRACT;      // Numpad - (subtract)
            case VK_DECIMAL: return KeyCode::KEY_NUMPAD_DECIMAL;        // Numpad . (decimal)
            case VK_DIVIDE: return KeyCode::KEY_NUMPAD_DIVIDE;          // Numpad / (divide)

            // Punctuation and symbol keys (OEM keys)
            case VK_OEM_1: return KeyCode::KEY_SEMICOLON;               // ; (semicolon) and : (colon)
            case VK_OEM_PLUS: return KeyCode::KEY_EQUALS;               // = (equals sign) and + (plus)
            case VK_OEM_COMMA: return KeyCode::KEY_COMMA;               // , (comma) and < (less than)
            case VK_OEM_MINUS: return KeyCode::KEY_MINUS;               // - (minus/hyphen) and _ (underscore)
            case VK_OEM_PERIOD: return KeyCode::KEY_PERIOD;             // . (period) and > (greater than)
            case VK_OEM_2: return KeyCode::KEY_SLASH;                   // / (forward slash) and ? (question mark)
            case VK_OEM_3: return KeyCode::KEY_GRAVE;                   // ` (grave accent/backtick) and ~ (tilde)
            case VK_OEM_4: return KeyCode::KEY_BRACKET_LEFT;            // [ (left bracket) and { (left brace)
            case VK_OEM_5: return KeyCode::KEY_BACKSLASH;               // \ (backslash) and | (pipe)
            case VK_OEM_6: return KeyCode::KEY_BRACKET_RIGHT;           // ] (right bracket) and } (right brace)
            case VK_OEM_7: return KeyCode::KEY_QUOTE;                   // ' (apostrophe/quote) and " (double quote)

            // Media control keys
            case VK_VOLUME_UP: return KeyCode::KEY_VOLUME_UP;           // Volume Up key
            case VK_VOLUME_DOWN: return KeyCode::KEY_VOLUME_DOWN;       // Volume Down key
            case VK_VOLUME_MUTE: return KeyCode::KEY_VOLUME_MUTE;       // Volume Mute key
            case VK_MEDIA_PLAY_PAUSE: return KeyCode::KEY_MEDIA_PLAY_PAUSE; // Media Play/Pause key
            case VK_MEDIA_STOP: return KeyCode::KEY_MEDIA_STOP;         // Media Stop key
            case VK_MEDIA_PREV_TRACK: return KeyCode::KEY_MEDIA_PREV;   // Media Previous Track key
            case VK_MEDIA_NEXT_TRACK: return KeyCode::KEY_MEDIA_NEXT;   // Media Next Track key

            // Browser navigation keys
            case VK_BROWSER_BACK: return KeyCode::KEY_BROWSER_BACK;     // Browser Back key
            case VK_BROWSER_FORWARD: return KeyCode::KEY_BROWSER_FORWARD; // Browser Forward key
            case VK_BROWSER_REFRESH: return KeyCode::KEY_BROWSER_REFRESH; // Browser Refresh key
            case VK_BROWSER_STOP: return KeyCode::KEY_BROWSER_STOP;     // Browser Stop key
            case VK_BROWSER_SEARCH: return KeyCode::KEY_BROWSER_SEARCH; // Browser Search key
            case VK_BROWSER_FAVORITES: return KeyCode::KEY_BROWSER_FAVORITES; // Browser Favorites key
            case VK_BROWSER_HOME: return KeyCode::KEY_BROWSER_HOME;     // Browser Home key

            // System control keys
            case VK_SNAPSHOT: return KeyCode::KEY_PRINT_SCREEN;         // Print Screen key
            case VK_PAUSE: return KeyCode::KEY_PAUSE;                   // Pause/Break key
            case VK_APPS: return KeyCode::KEY_MENU;                     // Menu/Context key (between right Alt and Ctrl)
            case VK_SLEEP: return KeyCode::KEY_SLEEP;                   // Sleep key

            // Default case for unmapped keys
            default: 
            {
                #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_WARNING, L"[KeyboardHandler] Unknown Windows VK code: 0x%08X", platformKey);
                #endif
                return KeyCode::KEY_UNKNOWN;                            // Unknown or unmapped key
            }
        }

    #elif defined(PLATFORM_LINUX)
        // X11 KeySym mapping for Linux
        switch (platformKey) {
            // Alphabet keys (both lowercase and uppercase variants)
            case XK_a: case XK_A: return KeyCode::KEY_A;                // Letter A key
            case XK_b: case XK_B: return KeyCode::KEY_B;                // Letter B key
            case XK_c: case XK_C: return KeyCode::KEY_C;                // Letter C key
            case XK_d: case XK_D: return KeyCode::KEY_D;                // Letter D key
            case XK_e: case XK_E: return KeyCode::KEY_E;                // Letter E key
            case XK_f: case XK_F: return KeyCode::KEY_F;                // Letter F key
            case XK_g: case XK_G: return KeyCode::KEY_G;                // Letter G key
            case XK_h: case XK_H: return KeyCode::KEY_H;                // Letter H key
            case XK_i: case XK_I: return KeyCode::KEY_I;                // Letter I key
            case XK_j: case XK_J: return KeyCode::KEY_J;                // Letter J key
            case XK_k: case XK_K: return KeyCode::KEY_K;                // Letter K key
            case XK_l: case XK_L: return KeyCode::KEY_L;                // Letter L key
            case XK_m: case XK_M: return KeyCode::KEY_M;                // Letter M key
            case XK_n: case XK_N: return KeyCode::KEY_N;                // Letter N key
            case XK_o: case XK_O: return KeyCode::KEY_O;                // Letter O key
            case XK_p: case XK_P: return KeyCode::KEY_P;                // Letter P key
            case XK_q: case XK_Q: return KeyCode::KEY_Q;                // Letter Q key
            case XK_r: case XK_R: return KeyCode::KEY_R;                // Letter R key
            case XK_s: case XK_S: return KeyCode::KEY_S;                // Letter S key
            case XK_t: case XK_T: return KeyCode::KEY_T;                // Letter T key
            case XK_u: case XK_U: return KeyCode::KEY_U;                // Letter U key
            case XK_v: case XK_V: return KeyCode::KEY_V;                // Letter V key
            case XK_w: case XK_W: return KeyCode::KEY_W;                // Letter W key
            case XK_x: case XK_X: return KeyCode::KEY_X;                // Letter X key
            case XK_y: case XK_Y: return KeyCode::KEY_Y;                // Letter Y key
            case XK_z: case XK_Z: return KeyCode::KEY_Z;                // Letter Z key

            // Number keys (top row)
            case XK_0: return KeyCode::KEY_0;                           // Number 0 key (top row)
            case XK_1: return KeyCode::KEY_1;                           // Number 1 key (top row)
            case XK_2: return KeyCode::KEY_2;                           // Number 2 key (top row)
            case XK_3: return KeyCode::KEY_3;                           // Number 3 key (top row)
            case XK_4: return KeyCode::KEY_4;                           // Number 4 key (top row)
            case XK_5: return KeyCode::KEY_5;                           // Number 5 key (top row)
            case XK_6: return KeyCode::KEY_6;                           // Number 6 key (top row)
            case XK_7: return KeyCode::KEY_7;                           // Number 7 key (top row)
            case XK_8: return KeyCode::KEY_8;                           // Number 8 key (top row)
            case XK_9: return KeyCode::KEY_9;                           // Number 9 key (top row)

            // Function keys
            case XK_F1: return KeyCode::KEY_F1;                         // Function key F1
            case XK_F2: return KeyCode::KEY_F2;                         // Function key F2
            case XK_F3: return KeyCode::KEY_F3;                         // Function key F3
            case XK_F4: return KeyCode::KEY_F4;                         // Function key F4
            case XK_F5: return KeyCode::KEY_F5;                         // Function key F5
            case XK_F6: return KeyCode::KEY_F6;                         // Function key F6
            case XK_F7: return KeyCode::KEY_F7;                         // Function key F7
            case XK_F8: return KeyCode::KEY_F8;                         // Function key F8
            case XK_F9: return KeyCode::KEY_F9;                         // Function key F9
            case XK_F10: return KeyCode::KEY_F10;                       // Function key F10
            case XK_F11: return KeyCode::KEY_F11;                       // Function key F11
            case XK_F12: return KeyCode::KEY_F12;                       // Function key F12

            // Modifier keys
            case XK_Shift_L: return KeyCode::KEY_SHIFT_LEFT;            // Left Shift key
            case XK_Shift_R: return KeyCode::KEY_SHIFT_RIGHT;           // Right Shift key
            case XK_Control_L: return KeyCode::KEY_CTRL_LEFT;           // Left Control key
            case XK_Control_R: return KeyCode::KEY_CTRL_RIGHT;          // Right Control key
            case XK_Alt_L: return KeyCode::KEY_ALT_LEFT;                // Left Alt key
            case XK_Alt_R: return KeyCode::KEY_ALT_RIGHT;               // Right Alt key

            // Navigation arrow keys
            case XK_Up: return KeyCode::KEY_ARROW_UP;                   // Up arrow key
            case XK_Down: return KeyCode::KEY_ARROW_DOWN;               // Down arrow key
            case XK_Left: return KeyCode::KEY_ARROW_LEFT;               // Left arrow key
            case XK_Right: return KeyCode::KEY_ARROW_RIGHT;             // Right arrow key

            // Navigation cluster keys
            case XK_Home: return KeyCode::KEY_HOME;                     // Home key
            case XK_End: return KeyCode::KEY_END;                       // End key
            case XK_Page_Up: return KeyCode::KEY_PAGE_UP;               // Page Up key
            case XK_Page_Down: return KeyCode::KEY_PAGE_DOWN;           // Page Down key

            // Special control keys
            case XK_space: return KeyCode::KEY_SPACE;                   // Space bar
            case XK_Return: return KeyCode::KEY_ENTER;                  // Enter/Return key
            case XK_BackSpace: return KeyCode::KEY_BACKSPACE;           // Backspace key
            case XK_Tab: return KeyCode::KEY_TAB;                       // Tab key
            case XK_Delete: return KeyCode::KEY_DELETE;                 // Delete key
            case XK_Insert: return KeyCode::KEY_INSERT;                 // Insert key
            case XK_Escape: return KeyCode::KEY_ESCAPE;                 // Escape key

            // LINUX NUMPAD KEYS - COMPLETE MAPPING
            case XK_KP_0: return KeyCode::KEY_NUMPAD_0;                 // Numpad 0
            case XK_KP_1: return KeyCode::KEY_NUMPAD_1;                 // Numpad 1
            case XK_KP_2: return KeyCode::KEY_NUMPAD_2;                 // Numpad 2
            case XK_KP_3: return KeyCode::KEY_NUMPAD_3;                 // Numpad 3
            case XK_KP_4: return KeyCode::KEY_NUMPAD_4;                 // Numpad 4
            case XK_KP_5: return KeyCode::KEY_NUMPAD_5;                 // Numpad 5
            case XK_KP_6: return KeyCode::KEY_NUMPAD_6;                 // Numpad 6
            case XK_KP_7: return KeyCode::KEY_NUMPAD_7;                 // Numpad 7
            case XK_KP_8: return KeyCode::KEY_NUMPAD_8;                 // Numpad 8
            case XK_KP_9: return KeyCode::KEY_NUMPAD_9;                 // Numpad 9
            case XK_KP_Multiply: return KeyCode::KEY_NUMPAD_MULTIPLY;   // Numpad * (multiply)
            case XK_KP_Add: return KeyCode::KEY_NUMPAD_ADD;             // Numpad + (add)
            case XK_KP_Subtract: return KeyCode::KEY_NUMPAD_SUBTRACT;   // Numpad - (subtract)
            case XK_KP_Decimal: return KeyCode::KEY_NUMPAD_DECIMAL;     // Numpad . (decimal)
            case XK_KP_Divide: return KeyCode::KEY_NUMPAD_DIVIDE;       // Numpad / (divide)
            case XK_KP_Enter: return KeyCode::KEY_NUMPAD_ENTER;         // Numpad Enter key

            // Default case for unmapped keys
            default: 
            {
                #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_WARNING, L"[KeyboardHandler] Unknown Linux KeySym: 0x%08X", platformKey);
                #endif
                return KeyCode::KEY_UNKNOWN;                            // Unknown or unmapped key
            }
        }
    #else
        // Unsupported platform fallback
        #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[KeyboardHandler] Unsupported platform for key mapping: 0x%08X", platformKey);
        #endif
        return KeyCode::KEY_UNKNOWN;                                    // Unknown platform
    #endif
}

// Get current modifier flags (Ctrl, Shift, Alt combinations)
uint32_t KeyboardHandler::GetCurrentModifierFlags() const {
   // Return current modifier flags atomically (thread-safe)
   return m_currentModifierFlags.load();
}

// Update key states (called by game loop for edge detection)
void KeyboardHandler::UpdateKeyStates() {
   // Update previous frame state for all keys (lock-free operation)
   for (auto& keyState : m_keyStates) {
       // Store current state as previous state for next frame
       keyState.wasPressed.store(keyState.isPressed.load());
   }
}

//==============================================================================
// Performance Monitoring Methods Implementation
//==============================================================================

// Get keyboard processing statistics
void KeyboardHandler::GetKeyboardStats(uint64_t& eventsProcessed, float& avgProcessingTime) const {
   // Get total events processed
   eventsProcessed = m_eventsProcessed.load();
   
   // Calculate average processing time
   uint64_t totalTime = m_processingTimeTotal.load();
   if (eventsProcessed > 0) {
       avgProcessingTime = static_cast<float>(totalTime) / static_cast<float>(eventsProcessed);
   }
   else {
       avgProcessingTime = 0.0f;                           // No events processed yet
   }
   
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logDebugMessage(LogLevel::LOG_DEBUG, 
           L"Keyboard stats - Events: %llu, Avg time: %.3f us", eventsProcessed, avgProcessingTime);
   #endif
}

// Get thread performance metrics
bool KeyboardHandler::GetThreadPerformanceMetrics(float& cpuUsage, uint64_t& memoryUsage) const {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Retrieving keyboard thread performance metrics");
   #endif
   
   try {
       // Initialize output parameters
       cpuUsage = 0.0f;
       memoryUsage = 0;
       
       // Check if keyboard thread exists and is running
       if (!threadManager.DoesThreadExist(THREAD_AI_PROCESSING)) {
           #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
               debug.logLevelMessage(LogLevel::LOG_WARNING, L"Keyboard thread does not exist - cannot get performance metrics");
           #endif
           return false;                                   // Thread doesn't exist
       }
       
       // Estimate memory usage
       memoryUsage += sizeof(KeyboardHandler);             // Size of keyboard handler object
       memoryUsage += m_keyStates.size() * sizeof(KeyState); // Size of key states array
       memoryUsage += m_keyLog.size() * sizeof(KeyLogEntry); // Size of key log
       memoryUsage += m_registeredHotkeys.size() * 64;     // Estimated size of hotkey map
       
       // Estimate CPU usage based on processing frequency
       auto currentTime = std::chrono::steady_clock::now();
       auto timeSinceLastUpdate = std::chrono::duration_cast<std::chrono::seconds>(
           currentTime - m_lastStatsUpdate);
           
       if (timeSinceLastUpdate.count() > 0) {
           // Calculate events per second
           uint64_t eventsProcessed = m_eventsProcessed.load();
           float eventsPerSecond = static_cast<float>(eventsProcessed) / static_cast<float>(timeSinceLastUpdate.count());
           
           // Estimate CPU usage based on event frequency (rough approximation)
           cpuUsage = std::min(eventsPerSecond / 1000.0f, 1.0f); // Cap at 100%
       }
       
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_DEBUG, 
               L"Performance metrics - CPU: %.1f%%, Memory: %llu bytes", cpuUsage * 100.0f, memoryUsage);
       #endif
       
       return true;                                        // Successfully retrieved metrics
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception getting performance metrics: %S", e.what());
       #endif
       
       // Reset output parameters on error
       cpuUsage = 0.0f;
       memoryUsage = 0;
       return false;                                       // Failed to get metrics
   }
}

//==============================================================================
// Private Threading Methods Implementation
//==============================================================================

// Main keyboard processing thread function
void KeyboardHandler::KeyboardThreadFunction() {
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard processing thread started - entering main loop");
   #endif
   
   try {
       // Set thread-specific initialization
       auto threadStartTime = std::chrono::steady_clock::now();
       uint64_t totalEventsProcessed = 0;
       
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard thread initialization completed - entering processing loop");
       #endif
       
       // Main keyboard processing loop - continues until shutdown requested
       while (!m_shouldShutdown.load()) {
           try {
               // Record loop iteration start time for performance monitoring
               auto loopStartTime = std::chrono::steady_clock::now();
               
               // Process platform-specific keyboard events
               ProcessKeyboardEvents();
               
               // Process key combinations and hotkeys
               ProcessKeyCombinations();
               
               // Update lock key states from OS
               UpdateLockKeyStates();
               
               // Update event processing counter
               totalEventsProcessed++;
               m_eventsProcessed.store(totalEventsProcessed);
               
               // Calculate processing time
               auto loopEndTime = std::chrono::steady_clock::now();
               auto processingTime = std::chrono::duration_cast<std::chrono::microseconds>(
                   loopEndTime - loopStartTime);
               
               // Update total processing time
               m_processingTimeTotal.fetch_add(static_cast<uint64_t>(processingTime.count()));
               
               // Sleep briefly to prevent CPU spinning (gaming-optimized timing)
               std::this_thread::sleep_for(std::chrono::microseconds(1000)); // 1ms sleep = 1000 Hz polling
               
           }
           catch (const std::exception& e) {
               #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                   debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception in keyboard thread main loop: %S", e.what());
               #endif
               
               // Continue operation unless it's a critical error
               std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Brief pause before retry
           }
       }
       
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_INFO, 
               L"Keyboard thread shutdown completed - Processed %llu events", totalEventsProcessed);
       #endif
       
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Fatal exception in keyboard thread: %S", e.what());
       #endif
   }
   
   #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logLevelMessage(LogLevel::LOG_INFO, L"Keyboard processing thread terminated");
   #endif
}

// Process platform-specific keyboard events
void KeyboardHandler::ProcessKeyboardEvents() {
   // Platform-specific keyboard event processing
   #if defined(PLATFORM_WINDOWS)
       // Windows message processing is handled by the hook callback
       // This function serves as a placeholder for additional processing
       
   #elif defined(PLATFORM_LINUX)
       // Linux X11 event processing
       if (m_display) {
           XEvent event;
           while (XPending(m_display)) {
               XNextEvent(m_display, &event);
               
               if (event.type == KeyPress || event.type == KeyRelease) {
                   KeySym keySym = XLookupKeysym(&event.xkey, 0);
                   KeyCode keyCode = PlatformKeyToKeyCode(static_cast<uint32_t>(keySym));
                   
                   if (keyCode != KeyCode::KEY_UNKNOWN) {
                       uint32_t modifierFlags = GetCurrentModifierFlags();
                       
                       if (event.type == KeyPress) {
                           HandleKeyDown(keyCode, modifierFlags);
                       }
                       else {
                           HandleKeyUp(keyCode, modifierFlags);
                       }
                   }
               }
           }
       }
       
   #elif defined(PLATFORM_MACOS)
       // macOS event processing is handled by the event tap callback
       // This function serves as a placeholder for additional processing
       
   #elif defined(PLATFORM_ANDROID) || defined(PLATFORM_IOS)
       // Mobile platform event processing would be handled through JNI/UIKit
       // This function serves as a placeholder for mobile-specific processing
       
   #endif
}

//==============================================================================
// Private Event Processing Methods Implementation
//==============================================================================

// Handle key down event internally
void KeyboardHandler::HandleKeyDown(KeyCode keyCode, uint32_t modifierFlags) {
   try {
       // Convert KeyCode to array index for fast access
       uint32_t keyIndex = static_cast<uint32_t>(keyCode) % m_keyStates.size();
       
       // Update key state atomically
       KeyState& keyState = m_keyStates[keyIndex];
       bool wasAlreadyPressed = keyState.isPressed.exchange(true);
       
       // Record press time if not already pressed
       if (!wasAlreadyPressed) {
           keyState.pressTime = std::chrono::steady_clock::now();
           keyState.repeatCount = 0;                       // Reset repeat count for new press
       }
       else if (m_config.enableKeyRepeat) {
           // Handle key repeat
           keyState.repeatCount++;
       }
       
       // Update modifier flags
       m_currentModifierFlags.store(modifierFlags);
       
       // Add to key log for AI system
       if (m_config.enableKeyLogging) {
           AddToKeyLog(keyCode, true, modifierFlags);
       }
       
       // Call custom key down handler if registered
       KeyDownHandler* handler = m_keyDownHandler.load();
       if (handler != nullptr) {
           try {
               (*handler)(keyCode, modifierFlags);
           }
           catch (const std::exception& e) {
               #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                   debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in custom key down handler: %S", e.what());
               #endif
           }
       }
       
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Key down: %S (0x%08X), Modifiers: 0x%08X", 
               KeyCodeToString(keyCode).c_str(), static_cast<uint32_t>(keyCode), modifierFlags);
       #endif
       
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception handling key down: %S", e.what());
       #endif
   }
}

// Handle key up event internally
void KeyboardHandler::HandleKeyUp(KeyCode keyCode, uint32_t modifierFlags) {
   try {
       // Convert KeyCode to array index for fast access
       uint32_t keyIndex = static_cast<uint32_t>(keyCode) % m_keyStates.size();
       
       // Update key state atomically
       KeyState& keyState = m_keyStates[keyIndex];
       keyState.isPressed.store(false);
       keyState.releaseTime = std::chrono::steady_clock::now();
       
       // Update modifier flags
       m_currentModifierFlags.store(modifierFlags);
       
       // Add to key log for AI system
       if (m_config.enableKeyLogging) {
           AddToKeyLog(keyCode, false, modifierFlags);
       }
       
       // Call custom key up handler if registered
       KeyUpHandler* handler = m_keyUpHandler.load();
       if (handler != nullptr) {
           try {
               (*handler)(keyCode, modifierFlags);
           }
           catch (const std::exception& e) {
               #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                   debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in custom key up handler: %S", e.what());
               #endif
           }
       }
       
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Key up: %S (0x%08X), Modifiers: 0x%08X", 
               KeyCodeToString(keyCode).c_str(), static_cast<uint32_t>(keyCode), modifierFlags);
       #endif
       
   }
   catch (const std::exception& e) {
       #if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception handling key up: %S", e.what());
       #endif
   }
}

// Process key combinations and hotkeys
void KeyboardHandler::ProcessKeyCombinations() {
   if (!m_config.enableMultiKeyDetection) {
       return;                                             // Multi-key detection disabled
   }
   
   try {
       // Collect currently pressed keys
       std::vector<KeyCode> pressedKeys;
       pressedKeys.reserve(m_config.maxCombinationKeys);
       
       // Scan through all key states to find pressed keys
       for (size_t i = 0; i < m_keyStates.size(); ++i) {
           if (m_keyStates[i].isPressed.load()) {
               // Convert index back to KeyCode
               KeyCode keyCode = static_cast<KeyCode>(i);
               pressedKeys.push_back(keyCode);
               
               // Limit combination size
               if (pressedKeys.size() >= m_config.maxCombinationKeys) {
                   break;
               }
           }
       }
       
       // Process hotkey combinations if any keys are pressed
       if (!pressedKeys.empty()) {
           // Check for registered hotkeys
           uint64_t comboHash = CalculateKeyComboHash(pressedKeys);
           
           ThreadLockHelper hotkeyLock(threadManager, "keyboard_hotkey_process", 100, true); // Silent lock
           if (hotkeyLock.IsLocked()) {
               auto it = m_registeredHotkeys.find(comboHash);
               if (it != m_registeredHotkeys.end()) {
                   // Execute hotkey callback
                   try {
                       it->second();                               // Call registered callback

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                       debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Hotkey combination executed - Hash: 0x%016llX", comboHash);
#endif
                   }
                   catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                       debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in hotkey callback: %S", e.what());
#endif
                   }
               }
           }

           // Call custom key combination handler if registered
           KeyComboHandler* handler = m_keyComboHandler.load();
           if (handler != nullptr) {
               try {
                   (*handler)(pressedKeys, m_currentModifierFlags.load());
               }
               catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
                   debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in custom key combo handler: %S", e.what());
#endif
               }
           }
       }

   }
   catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
       debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception processing key combinations: %S", e.what());
#endif
   }
}

// Add entry to key log for AI system
void KeyboardHandler::AddToKeyLog(KeyCode keyCode, bool isKeyDown, uint32_t modifierFlags) {
    if (!m_config.enableKeyLogging) {
        return;                                             // Key logging disabled
    }

    try {
        // Access key log with thread safety
        ThreadLockHelper logLock(threadManager, "keyboard_keylog_add", 100, true); // Silent lock
        if (!logLock.IsLocked()) {
            return;                                         // Failed to acquire lock, skip logging
        }

        // Create key log entry
        KeyLogEntry entry(keyCode, isKeyDown, modifierFlags);

        // Add entry to log queue
        m_keyLog.push(entry);

        // Maintain maximum log size (circular buffer behavior)
        while (m_keyLog.size() > MAX_KEY_LOG_ENTRIES) {
            m_keyLog.pop();                                 // Remove oldest entry
        }

        // Update total keys logged counter
        m_totalKeysLogged.fetch_add(1);

    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception adding to key log: %S", e.what());
#endif
    }
}

//==============================================================================
// Private Platform-Specific Methods Implementation
//==============================================================================

// Initialize platform-specific keyboard hooks
bool KeyboardHandler::InitializePlatformHooks() {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Initializing platform-specific keyboard hooks");
#endif

#if defined(PLATFORM_WINDOWS)
    // Initialize Windows keyboard hook
    try {
        // Install low-level keyboard hook
        m_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, WindowsKeyboardProc, GetModuleHandle(nullptr), 0);

        if (m_keyboardHook == nullptr) {
            DWORD error = GetLastError();
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to install Windows keyboard hook - Error: %d", error);
#endif
            return false;                               // Failed to install hook
        }

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Windows keyboard hook installed successfully");
#endif

        return true;                                    // Hook installed successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception installing Windows keyboard hook: %S", e.what());
#endif
        return false;                                   // Failed to install hook
    }

#elif defined(PLATFORM_LINUX)
    // Initialize Linux X11 keyboard handling
    try {
        // Open X11 display connection
        m_display = XOpenDisplay(nullptr);
        if (m_display == nullptr) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to open X11 display connection");
#endif
            return false;                               // Failed to open display
        }

        // Get root window for event capture
        m_rootWindow = DefaultRootWindow(m_display);

        // Select keyboard input events
        XSelectInput(m_display, m_rootWindow, KeyPressMask | KeyReleaseMask);

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Linux X11 keyboard handling initialized successfully");
#endif

        return true;                                    // X11 initialized successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception initializing Linux keyboard handling: %S", e.what());
#endif
        return false;                                   // Failed to initialize X11
    }

#elif defined(PLATFORM_MACOS)
    // Initialize macOS event tap
    try {
        // Create event tap for keyboard events
        CGEventMask eventMask = (1 << kCGEventKeyDown) | (1 << kCGEventKeyUp);
        m_eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap,
            kCGEventTapOptionDefault, eventMask, MacOSKeyboardCallback, this);

        if (m_eventTap == nullptr) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create macOS event tap");
#endif
            return false;                               // Failed to create event tap
        }

        // Create run loop source
        m_runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, m_eventTap, 0);

        // Add to current run loop
        CFRunLoopAddSource(CFRunLoopGetCurrent(), m_runLoopSource, kCFRunLoopCommonModes);

        // Enable event tap
        CGEventTapEnable(m_eventTap, true);

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"macOS event tap initialized successfully");
#endif

        return true;                                    // Event tap initialized successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception initializing macOS event tap: %S", e.what());
#endif
        return false;                                   // Failed to initialize event tap
    }

#elif defined(PLATFORM_ANDROID)
    // Initialize Android keyboard handling through JNI
    try {
        m_androidInitialized = true;                   // Set Android initialization flag

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Android keyboard handling initialized (placeholder)");
#endif

        return true;                                    // Android initialized (placeholder)
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception initializing Android keyboard handling: %S", e.what());
#endif
        return false;                                   // Failed to initialize Android
    }

#elif defined(PLATFORM_IOS)
    // Initialize iOS keyboard handling through UIKit
    try {
        m_iosInitialized = true;                       // Set iOS initialization flag

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"iOS keyboard handling initialized (placeholder)");
#endif

        return true;                                    // iOS initialized (placeholder)
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception initializing iOS keyboard handling: %S", e.what());
#endif
        return false;                                   // Failed to initialize iOS
    }

#else
    // Unsupported platform
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Platform-specific keyboard hooks not supported on this platform");
#endif
    return false;                                       // Platform not supported
#endif
}

// Clean up platform-specific keyboard hooks
void KeyboardHandler::CleanupPlatformHooks() {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Cleaning up platform-specific keyboard hooks");
#endif

#if defined(PLATFORM_WINDOWS)
    // Clean up Windows keyboard hook
    if (m_keyboardHook != nullptr) {
        if (UnhookWindowsHookEx(m_keyboardHook)) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Windows keyboard hook uninstalled successfully");
#endif
        }
        else {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to uninstall Windows keyboard hook - Error: %d", GetLastError());
#endif
        }
        m_keyboardHook = nullptr;                       // Clear hook handle
    }

#elif defined(PLATFORM_LINUX)
    // Clean up Linux X11 resources
    if (m_display != nullptr) {
        XCloseDisplay(m_display);                       // Close X11 display connection
        m_display = nullptr;

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Linux X11 display connection closed");
#endif
    }

#elif defined(PLATFORM_MACOS)
    // Clean up macOS event tap
    if (m_runLoopSource != nullptr) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), m_runLoopSource, kCFRunLoopCommonModes);
        CFRelease(m_runLoopSource);
        m_runLoopSource = nullptr;
    }

    if (m_eventTap != nullptr) {
        CGEventTapEnable(m_eventTap, false);            // Disable event tap
        CFRelease(m_eventTap);
        m_eventTap = nullptr;

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"macOS event tap cleaned up successfully");
#endif
    }

#elif defined(PLATFORM_ANDROID)
    // Clean up Android resources
    m_androidInitialized = false;                      // Clear Android initialization flag

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Android keyboard handling cleaned up");
#endif

#elif defined(PLATFORM_IOS)
    // Clean up iOS resources
    m_iosInitialized = false;                          // Clear iOS initialization flag

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"iOS keyboard handling cleaned up");
#endif
#endif
}

// Save current OS hotkey states
bool KeyboardHandler::SaveOSHotkeyStates() {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Saving current OS hotkey states");
#endif

#if defined(PLATFORM_WINDOWS)
    // Save Windows hotkey states
    try {
        // Clear previous saved hotkeys
        m_savedHotkeys.clear();

        // List of common Windows hotkeys to save/disable
        std::vector<UINT> commonHotkeys = {
            VK_LWIN,                                    // Left Windows key
            VK_RWIN,                                    // Right Windows key
            VK_APPS,                                    // Application key
            VK_TAB                                      // Alt+Tab (when combined with Alt)
        };

        // Save current hotkey registrations
        for (UINT hotkey : commonHotkeys) {
            m_savedHotkeys.push_back(hotkey);
        }

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Saved %zu Windows hotkey states", m_savedHotkeys.size());
#endif

        return true;                                    // Hotkey states saved successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception saving Windows hotkey states: %S", e.what());
#endif
        return false;                                   // Failed to save hotkey states
    }

#elif defined(PLATFORM_LINUX)
    // Save Linux hotkey states
    m_savedHotkeysState = true;                         // Simple state flag for Linux

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Linux hotkey states saved");
#endif

    return true;                                        // Linux hotkey states saved

#else
    // Other platforms - placeholder implementation
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Hotkey state saving not implemented for this platform");
#endif

    return true;                                        // Return success for unsupported platforms
#endif
}

// Restore OS hotkey states
bool KeyboardHandler::RestoreOSHotkeyStates() {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Restoring OS hotkey states");
#endif

#if defined(PLATFORM_WINDOWS)
    // Restore Windows hotkey states
    try {
        // Restore saved hotkey registrations
        for (UINT hotkey : m_savedHotkeys) {
            // Note: Actual hotkey restoration would require more complex state management
            // This is a simplified implementation
        }

        // Clear saved hotkeys
        m_savedHotkeys.clear();

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Windows hotkey states restored");
#endif

        return true;                                    // Hotkey states restored successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception restoring Windows hotkey states: %S", e.what());
#endif
        return false;                                   // Failed to restore hotkey states
    }

#elif defined(PLATFORM_LINUX)
    // Restore Linux hotkey states
    m_savedHotkeysState = false;                        // Clear state flag for Linux

#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Linux hotkey states restored");
#endif

    return true;                                        // Linux hotkey states restored

#else
    // Other platforms - placeholder implementation
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Hotkey state restoration not implemented for this platform");
#endif

    return true;                                        // Return success for unsupported platforms
#endif
}

// Update lock key states from OS
void KeyboardHandler::UpdateLockKeyStates() {
#if defined(PLATFORM_WINDOWS)
    // Update Windows lock key states
    try {
        // Get Caps Lock state
        bool capsLockState = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        m_lockStates.capsLockOn.store(capsLockState);

        // Get Num Lock state
        bool numLockState = (GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
        m_lockStates.numLockOn.store(numLockState);

        // Get Scroll Lock state
        bool scrollLockState = (GetKeyState(VK_SCROLL) & 0x0001) != 0;
        m_lockStates.scrollLockOn.store(scrollLockState);

    }
    catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception updating Windows lock key states: %S", e.what());
#endif
    }

#elif defined(PLATFORM_LINUX)
    // Update Linux lock key states using X11
    if (m_display != nullptr) {
        try {
            XkbStateRec state;
            if (XkbGetState(m_display, XkbUseCoreKbd, &state) == Success) {
                // Update lock key states based on X11 state
                m_lockStates.capsLockOn.store((state.locked_mods & LockMask) != 0);
                m_lockStates.numLockOn.store((state.locked_mods & Mod2Mask) != 0);
                m_lockStates.scrollLockOn.store(false); // Scroll lock handling varies on Linux
            }
        }
        catch (const std::exception& e) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception updating Linux lock key states: %S", e.what());
#endif
        }
    }

#else
    // Other platforms - maintain current state or use defaults
    // Lock key state management varies significantly across platforms
#endif
}

//==============================================================================
// Private Utility Methods Implementation
//==============================================================================

// Calculate hash for key combination (for hotkey storage)
uint64_t KeyboardHandler::CalculateKeyComboHash(const std::vector<KeyCode>& keys) const {
    // Sort keys to ensure consistent hash regardless of order
    std::vector<uint32_t> sortedKeys;
    sortedKeys.reserve(keys.size());

    for (KeyCode key : keys) {
        sortedKeys.push_back(static_cast<uint32_t>(key));
    }

    std::sort(sortedKeys.begin(), sortedKeys.end());

    // Calculate hash using FNV-1a algorithm
    uint64_t hash = 14695981039346656037ULL;                // FNV offset basis
    const uint64_t prime = 1099511628211ULL;               // FNV prime

    for (uint32_t key : sortedKeys) {
        hash ^= key;
        hash *= prime;
    }

    return hash;                                            // Return calculated hash
}

// Validate key combination for registration
bool KeyboardHandler::ValidateKeyCombo(const std::vector<KeyCode>& keys) const {
    // Check if combination is empty
    if (keys.empty()) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Key combination is empty");
#endif
        return false;                                       // Empty combination not allowed
    }

    // Check if combination exceeds maximum size
    if (keys.size() > m_config.maxCombinationKeys) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Key combination size %zu exceeds maximum %u",
            keys.size(), m_config.maxCombinationKeys);
#endif
        return false;                                       // Combination too large
    }

    // Check for duplicate keys
    std::vector<KeyCode> sortedKeys = keys;
    std::sort(sortedKeys.begin(), sortedKeys.end());
    auto it = std::unique(sortedKeys.begin(), sortedKeys.end());
    if (it != sortedKeys.end()) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Key combination contains duplicate keys");
#endif
        return false;                                       // Duplicate keys not allowed
    }

    // Check for unknown keys
    for (KeyCode key : keys) {
        if (key == KeyCode::KEY_UNKNOWN) {
#if defined(_DEBUG_KEYBOARDHANDLER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Key combination contains unknown key");
#endif
            return false;                                   // Unknown keys not allowed
        }
    }

    // Combination is valid
    return true;
}

//==============================================================================
// Platform-Specific Callback Functions
//==============================================================================

#if defined(PLATFORM_WINDOWS)
// Windows keyboard hook callback function
LRESULT CALLBACK KeyboardHandler::WindowsKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Process hook if code is valid
    if (nCode >= 0 && g_windowsKeyboardInstance != nullptr) {
        try {
            KBDLLHOOKSTRUCT* kbdStruct = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

            if (kbdStruct != nullptr) {
                // Convert Windows virtual key to our KeyCode
                KeyCode keyCode = g_windowsKeyboardInstance->PlatformKeyToKeyCode(kbdStruct->vkCode);

                if (keyCode != KeyCode::KEY_UNKNOWN) {
                    // Get current modifier flags
                    uint32_t modifierFlags = 0;
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifierFlags |= 0x01;
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifierFlags |= 0x02;
                    if (GetAsyncKeyState(VK_MENU) & 0x8000) modifierFlags |= 0x04;
                    if (GetAsyncKeyState(VK_LWIN) & 0x8000) modifierFlags |= 0x08;
                    if (GetAsyncKeyState(VK_RWIN) & 0x8000) modifierFlags |= 0x10;

                    // Handle key events
                    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                        g_windowsKeyboardInstance->HandleKeyDown(keyCode, modifierFlags);
                    }
                    else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                        g_windowsKeyboardInstance->HandleKeyUp(keyCode, modifierFlags);
                    }

                    // Block certain keys if hotkey blocking is enabled
                    if (g_windowsKeyboardInstance->m_config.enableHotKeyBlocking) {
                        // Block Windows key combinations (except Ctrl+Alt+Del for emergency)
                        if (kbdStruct->vkCode == VK_LWIN || kbdStruct->vkCode == VK_RWIN) {
                            // Allow Ctrl+Alt+Del for emergency access
                            bool isCtrlAltDel = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                                (GetAsyncKeyState(VK_MENU) & 0x8000) &&
                                (kbdStruct->vkCode == VK_DELETE);

                            if (!isCtrlAltDel) {
                                return 1;                       // Block this key combination
                            }
                        }
                    }
                }
            }
        }
        catch (const std::exception& e) {
            // Log exception but continue processing to prevent system instability
        }
    }

    // Call next hook in chain
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
#endif

#if defined(PLATFORM_MACOS)
// macOS event tap callback function
CGEventRef KeyboardHandler::MacOSKeyboardCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void* refcon) {
    KeyboardHandler* instance = static_cast<KeyboardHandler*>(refcon);

    if (instance != nullptr) {
        try {
            // Get key code from event
            CGKeyCode keyCode = static_cast<CGKeyCode>(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));

            // Convert to our KeyCode (simplified mapping)
            KeyCode ourKeyCode = instance->PlatformKeyToKeyCode(static_cast<uint32_t>(keyCode));

            if (ourKeyCode != KeyCode::KEY_UNKNOWN) {
                // Get modifier flags
                CGEventFlags flags = CGEventGetFlags(event);
                uint32_t modifierFlags = 0;
                if (flags & kCGEventFlagMaskCommand) modifierFlags |= 0x01;
                if (flags & kCGEventFlagMaskShift) modifierFlags |= 0x02;
                if (flags & kCGEventFlagMaskAlternate) modifierFlags |= 0x04;
                if (flags & kCGEventFlagMaskControl) modifierFlags |= 0x08;

                // Handle key events
                if (type == kCGEventKeyDown) {
                    instance->HandleKeyDown(ourKeyCode, modifierFlags);
                }
                else if (type == kCGEventKeyUp) {
                    instance->HandleKeyUp(ourKeyCode, modifierFlags);
                }
            }
        }
        catch (const std::exception& e) {
            // Log exception but continue processing
        }
    }

    // Return original event (don't block)
    return event;
}
#endif

#pragma warning(pop)