// -------------------------------------------------------------------------------------------------------------
// GamingAI.cpp - Comprehensive Gaming AI Intelligence System Implementation
// Section 1: Basic Structure, Constructor, Destructor and Core Initialization
// -------------------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "GamingAI.h"
#include "ThreadLockHelper.h"
#include "MathPrecalculation.h"

#include <intrin.h>

// External references required by the GamingAI system
extern Debug debug;
extern ThreadManager threadManager;
extern GamePlayer gamePlayer;

#if defined(__USING_JOYSTICKS__)
    extern Joystick js;
#endif

// Suppress specific warnings for optimized assembly-equivalent code
#pragma warning(push)
#pragma warning(disable: 4996)  // Suppress deprecated function warnings for intrinsics
#pragma warning(disable: 4127)  // Suppress conditional expression is constant for template optimizations
#pragma warning(disable: 4101)  // Suppress warning C4101: 'e': unreferenced local variable

//==============================================================================
// Constructor and Destructor Implementation
//==============================================================================

// Constructor - Initialize GamingAI system with default values
GamingAI::GamingAI() :
    m_isInitialized(false),                                             // System not initialized by default
    m_isMonitoring(false),                                              // Not monitoring by default
    m_analysisReady(false),                                             // No analysis ready initially
    m_shouldShutdown(false),                                            // No shutdown requested initially
    m_hasCleanedUp(false),                                              // Cleanup not performed by default
    m_commandsProcessed(0),                                             // No commands processed initially
    m_lastAnalysisTime(std::chrono::steady_clock::now()),               // Set current time as last analysis
    m_currentModelSize(0),                                              // No model data initially
    m_totalAnalysisCount(0),                                            // No analysis operations performed
    m_performanceStartTime(std::chrono::steady_clock::now()),           // Set performance monitoring start time
    m_sessionStartTime(std::chrono::steady_clock::now()),               // Set session start time
    m_currentSessionID(1),                                              // Start with session ID 1
    m_modelFilename("strategy.dat")                                     // Default model filename
{
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI constructor called - initializing AI intelligence system");
    #endif

    // Reserve memory for performance-critical containers to avoid reallocations
    m_sessionPlayerPositions.reserve(10000);                           // Reserve space for 10K position samples
    m_sessionInputEvents.reserve(5000);                                // Reserve space for 5K input events
    m_analysisTimings.reserve(1000);                                   // Reserve space for 1K timing samples

    // Initialize player analysis data container for maximum players
    m_playerAnalysisData.reserve(gamePlayer.GetActivePlayerCount());   // Reserve space based on active players

    // Initialize AI model data container
    m_aiModelData.reserve(1024 * 1024);                               // Reserve 1MB for initial model data

    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI constructor completed - system ready for initialization");
    #endif
}

// Destructor - Clean up GamingAI system resources
GamingAI::~GamingAI() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI destructor called - cleaning up AI intelligence system");
    #endif

    // Perform cleanup if not already done
    if (!m_hasCleanedUp.load()) {
        Cleanup();                                                      // Ensure proper cleanup
    }

    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI destructor completed - all resources cleaned up");
    #endif
}

//==============================================================================
// Core Initialization and Cleanup Methods
//==============================================================================

// Initialize the AI system with specified configuration
bool GamingAI::Initialize(const AIModelConfiguration& config) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::Initialize() called - starting AI system initialization");
    #endif

    // Prevent double initialization
    if (m_isInitialized.load()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"GamingAI already initialized - skipping");
        #endif
        return true;                                                    // Already initialized
    }

    // Use ThreadLockHelper for thread-safe initialization
    ThreadLockHelper initLock(threadManager, "gamingai_init", 5000);
    if (!initLock.IsLocked()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire initialization lock - cannot initialize GamingAI");
        #endif
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Store configuration settings
        m_configuration = config;                                       // Save AI configuration

        // Validate configuration parameters
        if (m_configuration.maxModelSizeBytes < 1024 * 1024) {          // Minimum 1MB model size
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Model size too small, setting to minimum 1MB");
            #endif
            m_configuration.maxModelSizeBytes = 1024 * 1024;            // Set minimum size
        }

        if (m_configuration.analysisIntervalSeconds < 10) {             // Minimum 10 second analysis interval
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Analysis interval too short, setting to minimum 10 seconds");
            #endif
            m_configuration.analysisIntervalSeconds = 10;               // Set minimum interval
        }

        // Initialize platform-specific performance timers
        #if defined(PLATFORM_WINDOWS)
        m_performanceTimer = CreateWaitableTimer(NULL, TRUE, NULL);     // Create Windows waitable timer
        if (m_performanceTimer == NULL) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create Windows performance timer");
            #endif
            return false;                                               // Failed to create timer
        }
        #elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS)
            clock_gettime(CLOCK_MONOTONIC, &m_performanceTimer);       // Initialize POSIX timer
        #endif

        // Initialize AI model data structures
        m_currentAnalysisResult = AIAnalysisResult();                   // Reset analysis results
        m_currentStrategy = EnemyAIStrategy();                          // Reset strategy
        m_currentModelSize.store(0);                                    // Reset model size

        // Attempt to load existing AI model from disk
        std::string modelPath = GetDefaultModelFilename();              // Get platform-specific model path
        if (ModelFileExists(modelPath)) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Existing AI model found at: %S", modelPath.c_str());
#endif

            if (!LoadAIModel(modelPath)) {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to load existing AI model - creating new model");
                #endif
                ResetAIModel();                                         // Create new model if loading fails
            }
        }
        else {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"No existing AI model found - creating new model");
            #endif
            ResetAIModel();                                             // Create new AI model
        }

        // Create and start AI processing thread using ThreadManager
        threadManager.SetThread(THREAD_AI_PROCESSING, [this]() {
            AIThreadTasking();                                          // Main AI thread function
            }, true);                                                   // Enable debug mode for AI thread

        // Mark system as successfully initialized
        m_isInitialized.store(true);                                    // Set initialization flag
        m_hasCleanedUp.store(false);                                    // Clear cleanup flag

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"GamingAI initialization completed successfully - Model size: %zu bytes, Analysis interval: %u seconds",
                m_configuration.maxModelSizeBytes, m_configuration.analysisIntervalSeconds);
        #endif

        return true;                                                    // Initialization successful
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during GamingAI initialization: %S", e.what());
        #endif
        return false;                                                   // Initialization failed
    }
}

// Clean up all AI resources and save current model
void GamingAI::Cleanup() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::Cleanup() called - cleaning up AI intelligence system");
    #endif

    // Prevent double cleanup
    if (m_hasCleanedUp.load()) {
        return;                                                         // Already cleaned up
    }

    try {
        // Signal AI thread to shutdown
        m_shouldShutdown.store(true);                                   // Set shutdown flag

        // Stop monitoring if currently active
        if (m_isMonitoring.load()) {
            EndMonitoring();                                            // Stop monitoring and save session data
        }

        // Clear command queue and notify waiting threads
        {
            ThreadLockHelper cleanupLock(threadManager, "gamingai_cleanup", 3000);
            if (cleanupLock.IsLocked()) {
                ClearCommandQueue();                                    // Clear all pending commands
                m_commandAvailableCV.notify_all();                     // Wake up waiting threads
            }
        }

        // Save current AI model to disk before shutdown
        if (m_isInitialized.load() && m_currentModelSize.load() > 0) {
            std::string modelPath = GetDefaultModelFilename();         // Get model save path
            if (!SaveAIModel(modelPath)) {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to save AI model during cleanup");
                #endif
            }
        }

        // Stop AI processing thread using ThreadManager
        if (threadManager.DoesThreadExist(THREAD_AI_PROCESSING)) {
            threadManager.StopThread(THREAD_AI_PROCESSING);             // Stop AI thread gracefully
        }

        // Clean up platform-specific resources
        #if defined(PLATFORM_WINDOWS)
            if (m_performanceTimer != NULL) {
                CloseHandle(m_performanceTimer);                       // Close Windows timer handle
                m_performanceTimer = NULL;
            }
        #endif

        // Clear all AI data structures
        {
            ThreadLockHelper dataLock(threadManager, "gamingai_data_cleanup", 2000);
            if (dataLock.IsLocked()) {
                m_playerAnalysisData.clear();                           // Clear player analysis data
                m_aiModelData.clear();                                  // Clear AI model data
                m_aiModelData.shrink_to_fit();                          // Free allocated memory
                m_sessionPlayerPositions.clear();                      // Clear session position data
                m_sessionPlayerPositions.shrink_to_fit();               // Free allocated memory
                m_sessionInputEvents.clear();                          // Clear session input data
                m_sessionInputEvents.shrink_to_fit();                   // Free allocated memory
                m_analysisTimings.clear();                              // Clear timing data
                m_analysisTimings.shrink_to_fit();                      // Free allocated memory
            }
        }

        // Mark system as cleaned up
        m_isInitialized.store(false);                                   // Clear initialization flag
        m_hasCleanedUp.store(true);                                     // Set cleanup flag

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI cleanup completed successfully");
        #endif
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during GamingAI cleanup: %S", e.what());
        #endif
    }
}

//==============================================================================
// Monitoring Control Methods Implementation
//==============================================================================

// Start monitoring player behavior for analysis
bool GamingAI::StartMonitoring() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::StartMonitoring() called - starting player behavior monitoring");
    #endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot start monitoring - GamingAI system not initialized");
        #endif
        return false;                                                   // System not initialized
    }

    // Check if already monitoring
    if (m_isMonitoring.load()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Already monitoring player behavior - ignoring start request");
        #endif
        return true;                                                    // Already monitoring
    }

    // Use ThreadLockHelper for thread-safe monitoring state change
    ThreadLockHelper monitorLock(threadManager, "gamingai_monitor_start", 3000);
    if (!monitorLock.IsLocked()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire monitoring lock - cannot start monitoring");
        #endif
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Initialize session tracking variables
        m_sessionStartTime = std::chrono::steady_clock::now();          // Record session start time
        m_currentSessionID.fetch_add(1);                               // Increment session ID counter

        // Clear previous session data to start fresh
        {
            ThreadLockHelper dataLock(threadManager, "gamingai_session_data", 2000);
            if (dataLock.IsLocked()) {
                m_sessionPlayerPositions.clear();                      // Clear previous position data
                m_sessionInputEvents.clear();                          // Clear previous input event data

                // Reserve memory for new session data collection
                m_sessionPlayerPositions.reserve(10000);               // Reserve space for position tracking
                m_sessionInputEvents.reserve(5000);                    // Reserve space for input tracking
            }
        }

        // Get current active players for monitoring setup
        int activePlayerCount = gamePlayer.GetActivePlayerCount();
        std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Setting up monitoring for %d active players", activePlayerCount);
        #endif

        // Initialize analysis data structures for each active player
        for (int playerID : activePlayerIDs) {
            // Create or reset player analysis data entry
            PlayerAnalysisData& playerData = m_playerAnalysisData[playerID];

            // Get player information from GamePlayer system
            const PlayerInfo* playerInfo = gamePlayer.GetPlayerInfo(playerID);
            if (playerInfo != nullptr) {
                playerData.playerID = static_cast<uint32_t>(playerID);  // Set player ID
                playerData.playerName = playerInfo->playerName;         // Copy player name
                playerData.lastAnalysisTime = std::chrono::system_clock::now(); // Set analysis time
                playerData.isDataValid = true;                         // Mark data as valid for analysis

                // Reset movement pattern data for new session
                playerData.movementData = PlayerMovementPattern();
                playerData.combatData = PlayerCombatPattern();
                playerData.inputData = PlayerInputPattern();

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Initialized monitoring for player %d: %S",
                        playerID, std::wstring(playerInfo->playerName.begin(), playerInfo->playerName.end()).c_str());
                #endif
            }
        }

        // Inject initial monitoring commands into AI processing queue
        InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT, AICommandPriority::PRIORITY_NORMAL);
        InjectAICommand(AICommandType::CMD_ANALYZE_INPUT_PATTERNS, AICommandPriority::PRIORITY_NORMAL);

        // Set monitoring flag to start data collection
        m_isMonitoring.store(true);                                     // Enable monitoring state

        // Update performance monitoring start time
        m_performanceStartTime = std::chrono::steady_clock::now();      // Reset performance tracking

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Player behavior monitoring started successfully - Session ID: %u, Active players: %d",
                m_currentSessionID.load(), activePlayerCount);
        #endif

        return true;                                                    // Monitoring started successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception starting monitoring: %S", e.what());
        #endif

        // Ensure monitoring flag is cleared on error
        m_isMonitoring.store(false);                                    // Clear monitoring state
        return false;                                                   // Failed to start monitoring
    }
}

// Stop monitoring and finalize current session data
bool GamingAI::EndMonitoring() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::EndMonitoring() called - stopping player behavior monitoring");
    #endif

    // Check if currently monitoring
    if (!m_isMonitoring.load()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Not currently monitoring - ignoring end request");
        #endif
        return true;                                                    // Not monitoring, nothing to stop
    }

    // Use ThreadLockHelper for thread-safe monitoring state change
    ThreadLockHelper monitorLock(threadManager, "gamingai_monitor_end", 3000);
    if (!monitorLock.IsLocked()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire monitoring lock - forcing monitoring stop");
        #endif

        // Force stop monitoring even without lock in emergency situations
        m_isMonitoring.store(false);                                    // Force clear monitoring state
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Calculate session duration for analysis
        auto sessionEndTime = std::chrono::steady_clock::now();
        auto sessionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            sessionEndTime - m_sessionStartTime);

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Ending monitoring session - Duration: %lld ms, Session ID: %u",
                    sessionDuration.count(), m_currentSessionID.load());
        #endif

        // Inject final analysis commands to process collected session data
        InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT, AICommandPriority::PRIORITY_HIGH);
        InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_COMBAT, AICommandPriority::PRIORITY_HIGH);
        InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_STRATEGY, AICommandPriority::PRIORITY_HIGH);
        InjectAICommand(AICommandType::CMD_GENERATE_ENEMY_STRATEGY, AICommandPriority::PRIORITY_HIGH);
        InjectAICommand(AICommandType::CMD_CLEAR_OUTDATED_DATA, AICommandPriority::PRIORITY_NORMAL);

        // Update session data for all monitored players
        {
            ThreadLockHelper dataLock(threadManager, "gamingai_session_finalize", 2000);
            if (dataLock.IsLocked()) {
                // Finalize analysis data for each player
                for (auto& playerPair : m_playerAnalysisData) {
                    PlayerAnalysisData& playerData = playerPair.second;

                    // Update session statistics
                    playerData.sessionsAnalyzed++;                     // Increment session count
                    playerData.movementData.sessionDuration = sessionDuration; // Store session duration
                    playerData.lastAnalysisTime = std::chrono::system_clock::now(); // Update analysis time

                    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"Finalized session data for player %u - Total sessions: %u",
                            playerData.playerID, playerData.sessionsAnalyzed);
                    #endif
                }

                // Calculate and store session performance metrics
                size_t positionSamples = m_sessionPlayerPositions.size();
                size_t inputSamples = m_sessionInputEvents.size();

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_INFO,
                        L"Session data collected - Position samples: %zu, Input samples: %zu",
                        positionSamples, inputSamples);
                #endif
            }
        }

        // Save AI model with updated session data if auto-save is enabled
        if (m_configuration.enableCrossSessionLearning) {
            // Inject model save command with normal priority
            InjectAICommand(AICommandType::CMD_SAVE_AI_MODEL, AICommandPriority::PRIORITY_NORMAL);
        }

        // Clear monitoring flag to stop data collection
        m_isMonitoring.store(false);                                    // Disable monitoring state

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Player behavior monitoring stopped successfully");
        #endif

        return true;                                                    // Monitoring stopped successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception stopping monitoring: %S", e.what());
        #endif

        // Ensure monitoring flag is cleared even on error
        m_isMonitoring.store(false);                                    // Force clear monitoring state
        return false;                                                   // Failed to stop monitoring cleanly
    }
}

//==============================================================================
// Real-time Data Collection Methods (Called during monitoring)
//==============================================================================

// Collect player position data during active monitoring
void GamingAI::CollectPlayerPositionData(uint32_t playerID, const Vector2& position) {
    // Only collect data if currently monitoring
    if (!m_isMonitoring.load()) {
        return;                                                         // Not monitoring, skip data collection
    }

    try {
        // Use fast, non-blocking lock for real-time data collection
        ThreadLockHelper dataLock(threadManager, "gamingai_position_collect", 100, true); // Silent lock with short timeout
        if (dataLock.IsLocked()) {
            // Add position to session tracking with timestamp
            m_sessionPlayerPositions.push_back(position);              // Store position data

            // Prevent unlimited memory growth by maintaining reasonable buffer size
            if (m_sessionPlayerPositions.size() > 15000) {             // Limit to 15K position samples
                // Remove oldest 5000 entries to maintain performance
                m_sessionPlayerPositions.erase(m_sessionPlayerPositions.begin(),
                    m_sessionPlayerPositions.begin() + 5000);

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Position buffer trimmed to maintain performance");
                #endif
            }

            // Update player-specific movement data
            auto playerIt = m_playerAnalysisData.find(playerID);
            if (playerIt != m_playerAnalysisData.end()) {
                PlayerAnalysisData& playerData = playerIt->second;

                // Add position to player's recent position history
                playerData.movementData.recentPositions.push_back(position);

                // Maintain reasonable history size for analysis
                if (playerData.movementData.recentPositions.size() > 500) {
                    playerData.movementData.recentPositions.erase(
                        playerData.movementData.recentPositions.begin(),
                        playerData.movementData.recentPositions.begin() + 100);
                }

                // Update movement sample count
                playerData.movementData.totalMovementSamples++;
            }
        }
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception collecting position data: %S", e.what());
        #endif
    }
}

// Collect player input event data during active monitoring
void GamingAI::CollectInputEventData(uint32_t inputType, uint32_t inputValue) {
    // Only collect data if currently monitoring
    if (!m_isMonitoring.load()) {
        return;                                                         // Not monitoring, skip data collection
    }

    try {
        // Use fast, non-blocking lock for real-time data collection
        ThreadLockHelper dataLock(threadManager, "gamingai_input_collect", 50, true); // Very short timeout for input
        if (dataLock.IsLocked()) {
            // Encode input type and value into single uint32_t for efficient storage
            uint32_t encodedInput = (inputType << 16) | (inputValue & 0xFFFF);
            m_sessionInputEvents.push_back(encodedInput);              // Store encoded input event

            // Prevent unlimited memory growth for input events
            if (m_sessionInputEvents.size() > 10000) {                 // Limit to 10K input events
                // Remove oldest 2000 entries to maintain performance
                m_sessionInputEvents.erase(m_sessionInputEvents.begin(),
                    m_sessionInputEvents.begin() + 2000);

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Input buffer trimmed to maintain performance");
                #endif
            }
        }
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception collecting input data: %S", e.what());
        #endif
    }
}

//==============================================================================
// AI Command Queue Methods Implementation
//==============================================================================

// Inject a command into the AI processing queue with priority
bool GamingAI::InjectAICommand(AICommandType commandType, AICommandPriority priority, uint32_t playerID, const std::string& commandData) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"GamingAI::InjectAICommand() called - Type: 0x%08X, Priority: %d, PlayerID: %u",
            static_cast<uint32_t>(commandType), static_cast<int>(priority), playerID);
    #endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot inject AI command - system not initialized");
    #endif
        return false;                                                   // System not initialized
    }

    // Check if shutdown is requested
    if (m_shouldShutdown.load()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Cannot inject AI command - system shutting down");
        #endif
        return false;                                                   // System shutting down
    }

    // Use ThreadLockHelper for thread-safe command queue access
    ThreadLockHelper queueLock(threadManager, "gamingai_command_queue", 2000);
    if (!queueLock.IsLocked()) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire command queue lock - command rejected");
        #endif
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Create AI command structure with provided parameters
        AICommand newCommand(commandType, priority, playerID, commandData);

        // Validate command parameters
        if (playerID > 0) {
                // Check if specified player ID is valid and active
                if (!gamePlayer.IsPlayerValid(static_cast<int>(playerID))) {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_WARNING,
                        L"Invalid player ID %u in AI command - using player 0", playerID);
                #endif
                newCommand.playerID = 0;                                // Default to player 0
            }
        }

        // Check command queue size to prevent memory exhaustion
        if (m_commandQueue.size() >= 1000) {                           // Maximum 1000 pending commands
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"AI command queue full - removing oldest low priority commands");
            #endif

            // Remove low priority commands to make space for new command
            std::priority_queue<AICommand> tempQueue;                  // Temporary queue for rebuilding
            int removedCount = 0;

            // Keep only high priority and critical commands
            while (!m_commandQueue.empty()) {
                AICommand cmd = m_commandQueue.top();
                m_commandQueue.pop();

                if (cmd.priority >= AICommandPriority::PRIORITY_HIGH || tempQueue.size() < 800) {
                    tempQueue.push(cmd);                                // Keep high priority or make space
                }
                else {
                    removedCount++;                                     // Count removed commands
                }
            }

            // Restore filtered commands to main queue
            m_commandQueue = std::move(tempQueue);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Removed %d low priority commands from queue", removedCount);
#endif
        }

        // Add command to priority queue
        m_commandQueue.push(newCommand);                                // Insert command with priority ordering

        // Handle emergency commands immediately
        if (priority >= AICommandPriority::PRIORITY_EMERGENCY) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Emergency AI command injected - immediate processing required");
#endif

            // Wake up AI thread immediately for emergency processing
            m_commandAvailableCV.notify_all();                         // Notify AI thread immediately
        }
        else if (priority >= AICommandPriority::PRIORITY_CRITICAL) {
            // Notify AI thread for critical commands
            m_commandAvailableCV.notify_one();                         // Notify AI thread
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"AI command injected successfully - Queue size: %zu, Command type: 0x%08X",
            m_commandQueue.size(), static_cast<uint32_t>(commandType));
#endif

        return true;                                                    // Command injected successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception injecting AI command: %S", e.what());
#endif
        return false;                                                   // Failed to inject command
    }
}

// Get current command queue size for monitoring
size_t GamingAI::GetCommandQueueSize() const {
    // Use ThreadLockHelper for thread-safe queue size access
    ThreadLockHelper queueLock(threadManager, "gamingai_queue_size", 500, true); // Silent with short timeout
    if (!queueLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for queue size - returning approximate size");
#endif
        return 0;                                                       // Cannot determine size safely
    }

    try {
        size_t queueSize = m_commandQueue.size();                      // Get current queue size

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"AI command queue size: %zu", queueSize);
#endif

        return queueSize;                                               // Return queue size
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception getting queue size: %S", e.what());
#endif
        return 0;                                                       // Return 0 on error
    }
}

// Clear all pending commands from queue
void GamingAI::ClearCommandQueue() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::ClearCommandQueue() called - clearing all pending commands");
#endif

    // Use ThreadLockHelper for thread-safe queue clearing
    ThreadLockHelper queueLock(threadManager, "gamingai_queue_clear", 3000);
    if (!queueLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock for queue clear - forcing clear");
#endif
        // Force clear even without lock in emergency situations
    }

    try {
        size_t clearedCommands = m_commandQueue.size();                // Count commands to be cleared

        // Clear the priority queue by creating new empty queue
        std::priority_queue<AICommand> emptyQueue;
        m_commandQueue = std::move(emptyQueue);                        // Replace with empty queue

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"AI command queue cleared - %zu commands removed", clearedCommands);
#endif
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception clearing command queue: %S", e.what());
#endif
    }
}

//==============================================================================
// AI Command Processing Methods (Private)
//==============================================================================

// Process individual AI commands from queue (called by AI thread)
void GamingAI::ProcessAICommand(const AICommand& command) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"Processing AI command - Type: 0x%08X, Priority: %d, PlayerID: %u",
        static_cast<uint32_t>(command.commandType), static_cast<int>(command.priority), command.playerID);
#endif

    // Record command processing start time for performance measurement
    auto processingStartTime = std::chrono::steady_clock::now();

    try {
        // Process command based on its type
        switch (command.commandType) {
        case AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing player movement analysis for player %u", command.playerID);
#endif

            if (command.playerID == 0) {
                // Analyze all active players
                std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();
                for (int playerID : activePlayerIDs) {
                    AnalyzePlayerMovement(static_cast<uint32_t>(playerID));
                }
            }
            else {
                // Analyze specific player
                AnalyzePlayerMovement(command.playerID);
            }
            break;
        }

        case AICommandType::CMD_ANALYZE_PLAYER_COMBAT:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing player combat analysis for player %u", command.playerID);
#endif

            if (command.playerID == 0) {
                // Analyze all active players
                std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();
                for (int playerID : activePlayerIDs) {
                    AnalyzePlayerCombat(static_cast<uint32_t>(playerID));
                }
            }
            else {
                // Analyze specific player
                AnalyzePlayerCombat(command.playerID);
            }
            break;
        }

        case AICommandType::CMD_ANALYZE_PLAYER_STRATEGY:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processing player strategy analysis");
#endif

            // Analyze overall player strategic patterns
            GenerateEnemyStrategy();                                // Generate strategic recommendations
            UpdateDifficultyRecommendations();                     // Update difficulty settings
            break;
        }

        case AICommandType::CMD_UPDATE_DIFFICULTY:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processing difficulty update command");
#endif

            UpdateDifficultyRecommendations();                     // Recalculate difficulty recommendations
            break;
        }

        case AICommandType::CMD_SAVE_AI_MODEL:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing AI model save command");
#endif

            std::string filename = command.commandData.empty() ? GetDefaultModelFilename() : command.commandData;
            if (!SaveModelToDisk(filename)) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to save AI model during command processing");
#endif
            }
            break;
        }

        case AICommandType::CMD_LOAD_AI_MODEL:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing AI model load command");
#endif

            std::string filename = command.commandData.empty() ? GetDefaultModelFilename() : command.commandData;
            if (!LoadModelFromDisk(filename)) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load AI model during command processing");
#endif
            }
            break;
        }

        case AICommandType::CMD_CLEAR_OUTDATED_DATA:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processing outdated data cleanup command");
#endif

            ClearOutdatedData();                                    // Remove old analysis data
            break;
        }

        case AICommandType::CMD_GENERATE_ENEMY_STRATEGY:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processing enemy strategy generation command");
#endif

            GenerateEnemyStrategy();                                // Generate new enemy strategies
            break;
        }

        case AICommandType::CMD_ANALYZE_INPUT_PATTERNS:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing input pattern analysis for player %u", command.playerID);
#endif

            if (command.playerID == 0) {
                // Analyze all active players
                std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();
                for (int playerID : activePlayerIDs) {
                    AnalyzePlayerInput(static_cast<uint32_t>(playerID));
                }
            }
            else {
                // Analyze specific player
                AnalyzePlayerInput(command.playerID);
            }
            break;
        }

        case AICommandType::CMD_PREDICT_PLAYER_ACTION:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processing player action prediction for player %u", command.playerID);
#endif

            // Predict next player action using historical patterns
            Vector2 predictedAction = PredictPlayerNextAction(command.playerID);

            // Store prediction results in current analysis data
            auto playerIt = m_playerAnalysisData.find(command.playerID);
            if (playerIt != m_playerAnalysisData.end()) {
                // Update player analysis with prediction data
                PlayerAnalysisData& playerData = playerIt->second;
                // Prediction results could be stored in a future enhancement
            }
            break;
        }

        case AICommandType::CMD_EMERGENCY_SHUTDOWN:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Processing emergency shutdown command");
#endif

            // Set shutdown flag and force immediate cleanup
            m_shouldShutdown.store(true);                           // Signal shutdown

            // Save critical data before shutdown
            if (m_currentModelSize.load() > 0) {
                SaveModelToDisk(GetDefaultModelFilename());         // Emergency model save
            }
            break;
        }

        default:
        {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"Unknown AI command type: 0x%08X - ignoring command", static_cast<uint32_t>(command.commandType));
#endif
            break;
        }
        }

        // Update command processing statistics
        m_commandsProcessed.fetch_add(1);                               // Increment processed command counter

        // Record processing time for performance monitoring
        auto processingEndTime = std::chrono::steady_clock::now();
        auto processingDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            processingEndTime - processingStartTime);

        // Store timing data for performance analysis
        {
            ThreadLockHelper timingLock(threadManager, "gamingai_timing", 100, true); // Silent timing lock
            if (timingLock.IsLocked()) {
                m_analysisTimings.push_back(processingDuration);       // Store processing time

                // Maintain reasonable timing history size
                if (m_analysisTimings.size() > 1000) {
                    m_analysisTimings.erase(m_analysisTimings.begin(), m_analysisTimings.begin() + 200);
                }
            }
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"AI command processed successfully - Duration: %lld ms, Total processed: %zu",
            processingDuration.count(), m_commandsProcessed.load());
#endif
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION,
            L"Exception processing AI command 0x%08X: %S", static_cast<uint32_t>(command.commandType), e.what());
#endif
    }
}

//==============================================================================
// Analysis Result Methods Implementation
//==============================================================================

// Return comprehensive AI analysis results (thread-safe)
AIAnalysisResult GamingAI::ReturnAIAnalysis() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::ReturnAIAnalysis() called - retrieving current analysis results");
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot return analysis - GamingAI system not initialized");
#endif

        // Return empty result with error indication
        AIAnalysisResult emptyResult;
        emptyResult.isAnalysisValid = false;
        emptyResult.analysisNotes = "AI system not initialized";
        return emptyResult;
    }

    // Use ThreadLockHelper for thread-safe analysis data access with extended timeout
    ThreadLockHelper analysisLock(threadManager, "gamingai_analysis_result", 5000);
    if (!analysisLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire analysis lock - returning last known results");
#endif

        // Return last known analysis with warning
        AIAnalysisResult warningResult = m_currentAnalysisResult;
        warningResult.analysisNotes += " [WARNING: Lock timeout - data may be stale]";
        return warningResult;
    }

    try {
        // Create comprehensive analysis result structure
        AIAnalysisResult analysisResult;

        // Set basic result metadata
        analysisResult.isAnalysisValid = true;                         // Mark as valid analysis
        analysisResult.analysisTimestamp = std::chrono::steady_clock::now(); // Set current timestamp
        analysisResult.analysisVersion = m_currentAnalysisResult.analysisVersion + 1; // Increment version

        // Get current active players for analysis
        std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();
        analysisResult.analyzedPlayerCount = static_cast<uint32_t>(activePlayerIDs.size());

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Preparing analysis results for %u active players", analysisResult.analyzedPlayerCount);
#endif

        // Clear and reserve space for player analysis data
        analysisResult.playerAnalysis.clear();
        analysisResult.playerAnalysis.reserve(activePlayerIDs.size());

        // Compile analysis data for each active player
        float totalSkillLevel = 0.0f;                                  // For calculating average skill
        uint32_t validPlayerCount = 0;                                 // Count of players with valid data

        for (int playerID : activePlayerIDs) {
            auto playerIt = m_playerAnalysisData.find(static_cast<uint32_t>(playerID));
            if (playerIt != m_playerAnalysisData.end()) {
                const PlayerAnalysisData& playerData = playerIt->second;

                // Validate player data quality before including in results
                if (ValidateAnalysisData(playerData)) {
                    // Copy player analysis data to result
                    analysisResult.playerAnalysis.push_back(playerData);

                    // Accumulate skill levels for average calculation
                    totalSkillLevel += static_cast<float>(playerData.skillLevel);
                    validPlayerCount++;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"Added player %u analysis - Skill: %u, Sessions: %u",
                        playerData.playerID, playerData.skillLevel, playerData.sessionsAnalyzed);
#endif
                }
                else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_WARNING,
                        L"Player %u analysis data failed validation - excluding from results",
                        static_cast<uint32_t>(playerID));
#endif
                }
            }
        }

        // Calculate overall difficulty recommendation based on player skill levels
        if (validPlayerCount > 0) {
            float averageSkillLevel = totalSkillLevel / static_cast<float>(validPlayerCount);

            // Use MathPrecalculation for smooth difficulty curve calculation
            float normalizedSkill = averageSkillLevel / 100.0f;         // Normalize to [0, 1] range

            // Apply smooth step interpolation for difficulty curve
            analysisResult.overallDifficultyRecommendation = FAST_MATH.FastSmoothStep(0.1f, 0.9f, normalizedSkill);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Calculated difficulty recommendation: %.3f (based on average skill: %.1f)",
                analysisResult.overallDifficultyRecommendation, averageSkillLevel);
#endif
        }
        else {
            // Default difficulty if no valid player data
            analysisResult.overallDifficultyRecommendation = 0.5f;      // Medium difficulty default

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"No valid player data - using default difficulty");
#endif
        }

        // Copy current enemy strategy recommendations
        analysisResult.recommendedStrategy = m_currentStrategy;

        // Generate comprehensive analysis notes
        std::ostringstream notesStream;
        notesStream << "Analysis based on " << validPlayerCount << " players. ";
        notesStream << "Total analysis operations: " << m_totalAnalysisCount.load() << ". ";

        // Add performance metrics to notes
        if (!m_analysisTimings.empty()) {
            // Calculate average analysis time using MathPrecalculation
            float totalTime = 0.0f;
            for (const auto& timing : m_analysisTimings) {
                totalTime += static_cast<float>(timing.count());
            }
            float averageTime = totalTime / static_cast<float>(m_analysisTimings.size());

            notesStream << "Average analysis time: " << std::fixed << std::setprecision(2) << averageTime << "ms. ";
        }

        // Add monitoring status to notes
        if (m_isMonitoring.load()) {
            auto monitorDuration = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - m_sessionStartTime);
            notesStream << "Currently monitoring (Session duration: " << monitorDuration.count() << "s). ";
        }

        // Add model size information
        size_t currentModelSize = m_currentModelSize.load();
        notesStream << "AI model size: " << (currentModelSize / 1024) << "KB";
        if (currentModelSize > m_configuration.maxModelSizeBytes * 0.8f) {
            notesStream << " [WARNING: Approaching size limit]";
        }

        analysisResult.analysisNotes = notesStream.str();

        // Update stored analysis result for future reference
        m_currentAnalysisResult = analysisResult;

        // Mark analysis as ready for external consumption
        m_analysisReady.store(true);

        // Increment total analysis count for statistics
        m_totalAnalysisCount.fetch_add(1);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Analysis results compiled successfully - Players: %u, Difficulty: %.3f, Valid: %s",
            analysisResult.analyzedPlayerCount, analysisResult.overallDifficultyRecommendation,
            analysisResult.isAnalysisValid ? L"Yes" : L"No");
#endif

        return analysisResult;                                          // Return complete analysis result

    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception generating analysis results: %S", e.what());
#endif

        // Return error result on exception
        AIAnalysisResult errorResult;
        errorResult.isAnalysisValid = false;
        errorResult.analysisNotes = "Exception occurred during analysis generation";
        errorResult.analysisTimestamp = std::chrono::steady_clock::now();
        return errorResult;
    }
}

// Force immediate analysis update
bool GamingAI::ForceAnalysisUpdate() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::ForceAnalysisUpdate() called - forcing immediate analysis");
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot force analysis update - system not initialized");
#endif
        return false;                                                   // System not initialized
    }

    try {
        // Inject high-priority analysis commands for immediate processing
        bool allCommandsInjected = true;

        // Force player movement analysis
        if (!InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject movement analysis command");
#endif
        }

        // Force player combat analysis
        if (!InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_COMBAT, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject combat analysis command");
#endif
        }

        // Force input pattern analysis
        if (!InjectAICommand(AICommandType::CMD_ANALYZE_INPUT_PATTERNS, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject input analysis command");
#endif
        }

        // Force strategy generation
        if (!InjectAICommand(AICommandType::CMD_GENERATE_ENEMY_STRATEGY, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject strategy generation command");
#endif
        }

        // Force difficulty update
        if (!InjectAICommand(AICommandType::CMD_UPDATE_DIFFICULTY, AICommandPriority::PRIORITY_HIGH)) {
            allCommandsInjected = false;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to inject difficulty update command");
#endif
        }

        // Wake up AI thread to process commands immediately
        {
            ThreadLockHelper notifyLock(threadManager, "gamingai_force_notify", 1000);
            if (notifyLock.IsLocked()) {
                m_commandAvailableCV.notify_all();                     // Wake up AI processing thread

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"AI thread notified for immediate analysis processing");
#endif
            }
        }

        // Wait briefly for processing to begin (non-blocking check)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));    // Brief wait for processing start

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Force analysis update completed - Commands injected: %s, Queue size: %zu",
            allCommandsInjected ? L"Success" : L"Partial", GetCommandQueueSize());
#endif

        return allCommandsInjected;                                     // Return success status
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception forcing analysis update: %S", e.what());
#endif
        return false;                                                   // Failed to force update
    }
}

//==============================================================================
// Analysis Data Validation and Quality Assurance Methods
//==============================================================================

// Validate player analysis data quality before including in results
bool GamingAI::ValidateAnalysisData(const PlayerAnalysisData& data) const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Validating analysis data for player %u", data.playerID);
#endif

    try {
        // Check basic data validity flags
        if (!data.isDataValid) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %u data marked as invalid", data.playerID);
#endif
            return false;                                               // Data marked as invalid
        }

        // Validate player ID range
        if (data.playerID >= 100) {                                     // Reasonable player ID limit
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player ID %u exceeds reasonable range", data.playerID);
#endif
            return false;                                               // Invalid player ID
        }

        // Validate skill level range
        if (data.skillLevel > 100) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %u skill level %u exceeds maximum",
                data.playerID, data.skillLevel);
#endif
            return false;                                               // Invalid skill level
        }

        // Validate adaptability factor range
        if (data.adaptabilityFactor < 0.0f || data.adaptabilityFactor > 1.0f) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %u adaptability factor %.3f out of range",
                data.playerID, data.adaptabilityFactor);
#endif
            return false;                                               // Invalid adaptability factor
        }

        // Validate movement data
        if (data.movementData.movementPredictability < 0.0f || data.movementData.movementPredictability > 1.0f) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %u movement predictability %.3f out of range",
                data.playerID, data.movementData.movementPredictability);
#endif
            return false;                                               // Invalid movement predictability
        }

        // Validate reaction time (should be reasonable for human players)
        if (data.movementData.reactionTime < 50.0f || data.movementData.reactionTime > 5000.0f) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %u reaction time %.1fms unrealistic",
                data.playerID, data.movementData.reactionTime);
#endif
            return false;                                               // Unrealistic reaction time
        }

        // Validate combat data
        if (data.combatData.accuracyPercentage < 0.0f || data.combatData.accuracyPercentage > 1.0f) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %u accuracy %.3f out of range",
                data.playerID, data.combatData.accuracyPercentage);
#endif
            return false;                                               // Invalid accuracy percentage
        }

        // Validate that player has sufficient data for meaningful analysis
        if (data.sessionsAnalyzed == 0) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %u has no analyzed sessions", data.playerID);
#endif
            return false;                                               // No session data available
        }

        // Check data freshness (analysis should be recent)
        auto currentTime = std::chrono::system_clock::now();
        auto dataAge = std::chrono::duration_cast<std::chrono::hours>(currentTime - data.lastAnalysisTime);

        if (dataAge.count() > 72) {                                     // Data older than 72 hours
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %u analysis data is %lld hours old",
                data.playerID, dataAge.count());
#endif
            return false;                                               // Data too old
        }

        // Validate movement sample count
        if (data.movementData.totalMovementSamples < 10) {              // Minimum samples for valid analysis
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %u has insufficient movement samples: %u",
                data.playerID, data.movementData.totalMovementSamples);
#endif
            return false;                                               // Insufficient movement data
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %u analysis data validation passed", data.playerID);
#endif

        return true;                                                    // All validation checks passed
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception validating player %u data: %S",
            data.playerID, e.what());
#endif
        return false;                                                   // Failed validation due to exception
    }
}

//==============================================================================
// Performance Monitoring Methods for Analysis
//==============================================================================

// Get average analysis processing time
std::chrono::milliseconds GamingAI::GetAverageAnalysisTime() const {
    // Use ThreadLockHelper for thread-safe access to timing data
    ThreadLockHelper timingLock(threadManager, "gamingai_avg_timing", 1000, true); // Silent with timeout
    if (!timingLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire timing lock - returning zero");
#endif
        return std::chrono::milliseconds(0);                           // Cannot access timing data
    }

    try {
        if (m_analysisTimings.empty()) {
            return std::chrono::milliseconds(0);                       // No timing data available
        }

        // Calculate average using MathPrecalculation for precision
        uint64_t totalTime = 0;
        for (const auto& timing : m_analysisTimings) {
            totalTime += static_cast<uint64_t>(timing.count());
        }

        uint64_t averageTime = totalTime / m_analysisTimings.size();

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Average analysis time calculated: %llu ms (from %zu samples)",
            averageTime, m_analysisTimings.size());
#endif

        return std::chrono::milliseconds(averageTime);
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception calculating average analysis time: %S", e.what());
#endif
        return std::chrono::milliseconds(0);                           // Return zero on error
    }
}

// Get AI thread performance metrics
bool GamingAI::GetThreadPerformanceMetrics(float& cpuUsage, uint64_t& memoryUsage) const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Retrieving AI thread performance metrics");
#endif

    try {
        // Initialize output parameters
        cpuUsage = 0.0f;
        memoryUsage = 0;

        // Check if AI thread exists and is running
        if (!threadManager.DoesThreadExist(THREAD_AI_PROCESSING)) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"AI thread does not exist - cannot get performance metrics");
#endif
            return false;                                               // Thread doesn't exist
        }

        // Get current model size as memory usage approximation
        memoryUsage = m_currentModelSize.load();

        // Add estimated memory usage for analysis structures
        memoryUsage += m_playerAnalysisData.size() * sizeof(PlayerAnalysisData);
        memoryUsage += m_sessionPlayerPositions.size() * sizeof(Vector2);
        memoryUsage += m_sessionInputEvents.size() * sizeof(uint32_t);
        memoryUsage += m_analysisTimings.size() * sizeof(std::chrono::milliseconds);

        // Estimate CPU usage based on recent processing activity
        auto currentTime = std::chrono::steady_clock::now();
        auto timeSinceLastAnalysis = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - m_lastAnalysisTime);

        // Calculate CPU usage estimate based on analysis frequency and timing
        if (timeSinceLastAnalysis.count() < m_configuration.analysisIntervalSeconds * 2) {
            // Recent activity detected
            if (!m_analysisTimings.empty()) {
                // Estimate CPU usage based on average analysis time vs interval
                uint64_t avgAnalysisMs = static_cast<uint64_t>(GetAverageAnalysisTime().count());
                uint64_t intervalMs = static_cast<uint64_t>(m_configuration.analysisIntervalSeconds) * 1000;

                cpuUsage = static_cast<float>(avgAnalysisMs) / static_cast<float>(intervalMs);
                cpuUsage = std::min(cpuUsage, 1.0f);                   // Cap at 100%
            }
            else {
                cpuUsage = 0.1f;                                        // Minimal usage estimate
            }
        }
        else {
            cpuUsage = 0.05f;                                           // Low usage when inactive
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Performance metrics - CPU: %.1f%%, Memory: %llu bytes",
            cpuUsage * 100.0f, memoryUsage);
#endif

        return true;                                                    // Successfully retrieved metrics
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception getting performance metrics: %S", e.what());
#endif

        // Reset output parameters on error
        cpuUsage = 0.0f;
        memoryUsage = 0;
        return false;                                                   // Failed to get metrics
    }
}

//==============================================================================
// Configuration Management Methods Implementation
//==============================================================================

// Update AI configuration settings
bool GamingAI::UpdateConfiguration(const AIModelConfiguration& config) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::UpdateConfiguration() called - updating AI configuration settings");
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot update configuration - GamingAI system not initialized");
#endif
        return false;                                                   // System not initialized
    }

    // Use ThreadLockHelper for thread-safe configuration update
    ThreadLockHelper configLock(threadManager, "gamingai_config_update", 3000);
    if (!configLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire configuration lock - update rejected");
#endif
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Store current configuration for rollback if needed
        AIModelConfiguration previousConfig = m_configuration;

        // Validate new configuration parameters before applying
        AIModelConfiguration validatedConfig = config;                 // Copy for validation

        // Validate maximum model size (minimum 1MB, maximum 2GB)
        if (validatedConfig.maxModelSizeBytes < 1024 * 1024) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Model size too small, adjusting to minimum 1MB");
#endif
            validatedConfig.maxModelSizeBytes = 1024 * 1024;           // Set minimum 1MB
        }
        else if (validatedConfig.maxModelSizeBytes > 2ULL * 1024 * 1024 * 1024) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Model size too large, adjusting to maximum 2GB");
#endif
            validatedConfig.maxModelSizeBytes = 2ULL * 1024 * 1024 * 1024; // Set maximum 2GB
        }

        // Validate analysis interval (minimum 5 seconds, maximum 300 seconds)
        if (validatedConfig.analysisIntervalSeconds < 5) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Analysis interval too short, adjusting to minimum 5 seconds");
#endif
            validatedConfig.analysisIntervalSeconds = 5;               // Set minimum 5 seconds
        }
        else if (validatedConfig.analysisIntervalSeconds > 300) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Analysis interval too long, adjusting to maximum 300 seconds");
#endif
            validatedConfig.analysisIntervalSeconds = 300;             // Set maximum 5 minutes
        }

        // Validate data retention period (minimum 1 day, maximum 365 days)
        if (validatedConfig.dataRetentionDays < 1.0f) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Data retention too short, adjusting to minimum 1 day");
#endif
            validatedConfig.dataRetentionDays = 1.0f;                  // Set minimum 1 day
        }
        else if (validatedConfig.dataRetentionDays > 365.0f) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Data retention too long, adjusting to maximum 365 days");
#endif
            validatedConfig.dataRetentionDays = 365.0f;                // Set maximum 1 year
        }

        // Validate learning rate (must be between 0.01 and 1.0)
        if (validatedConfig.learningRate < 0.01f) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Learning rate too low, adjusting to minimum 0.01");
#endif
            validatedConfig.learningRate = 0.01f;                      // Set minimum learning rate
        }
        else if (validatedConfig.learningRate > 1.0f) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Learning rate too high, adjusting to maximum 1.0");
#endif
            validatedConfig.learningRate = 1.0f;                       // Set maximum learning rate
        }

        // Validate max player history entries (minimum 100, maximum 10000)
        if (validatedConfig.maxPlayerHistoryEntries < 100) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Player history entries too few, adjusting to minimum 100");
#endif
            validatedConfig.maxPlayerHistoryEntries = 100;             // Set minimum entries
        }
        else if (validatedConfig.maxPlayerHistoryEntries > 10000) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Player history entries too many, adjusting to maximum 10000");
#endif
            validatedConfig.maxPlayerHistoryEntries = 10000;           // Set maximum entries
        }

        // Check if current model size exceeds new limit
        size_t currentModelSize = m_currentModelSize.load();
        if (currentModelSize > validatedConfig.maxModelSizeBytes) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"Current model size (%zu bytes) exceeds new limit (%zu bytes) - cleanup required",
                currentModelSize, validatedConfig.maxModelSizeBytes);
#endif

            // Inject command to clean outdated data to reduce model size
            InjectAICommand(AICommandType::CMD_CLEAR_OUTDATED_DATA, AICommandPriority::PRIORITY_HIGH);
        }

        // Apply validated configuration
        m_configuration = validatedConfig;

        // Log configuration changes for debugging
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Configuration updated successfully:");
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Max model size: %zu bytes (%.1f MB)",
            m_configuration.maxModelSizeBytes,
            static_cast<float>(m_configuration.maxModelSizeBytes) / (1024.0f * 1024.0f));
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Analysis interval: %u seconds",
            m_configuration.analysisIntervalSeconds);
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Data retention: %.1f days",
            m_configuration.dataRetentionDays);
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Learning rate: %.3f",
            m_configuration.learningRate);
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Max history entries: %u",
            m_configuration.maxPlayerHistoryEntries);
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Advanced prediction: %s",
            m_configuration.enableAdvancedPrediction ? L"Enabled" : L"Disabled");
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Cross-session learning: %s",
            m_configuration.enableCrossSessionLearning ? L"Enabled" : L"Disabled");
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"  Real-time analysis: %s",
            m_configuration.enableRealTimeAnalysis ? L"Enabled" : L"Disabled");
#endif

        return true;                                                    // Configuration updated successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception updating configuration: %S", e.what());
#endif
        return false;                                                   // Failed to update configuration
    }
}

// Get current AI configuration
AIModelConfiguration GamingAI::GetConfiguration() const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GamingAI::GetConfiguration() called - retrieving current configuration");
#endif

    // Use ThreadLockHelper for thread-safe configuration access
    ThreadLockHelper configLock(threadManager, "gamingai_config_get", 1000, true); // Silent with short timeout
    if (!configLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire configuration lock - returning default config");
#endif
        return AIModelConfiguration();                                  // Return default configuration
    }

    try {
        // Return copy of current configuration
        AIModelConfiguration currentConfig = m_configuration;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Configuration retrieved - Model size: %zu bytes, Analysis interval: %u seconds",
            currentConfig.maxModelSizeBytes, currentConfig.analysisIntervalSeconds);
#endif

        return currentConfig;                                           // Return current configuration
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception getting configuration: %S", e.what());
#endif
        return AIModelConfiguration();                                  // Return default on error
    }
}

// Set maximum AI model size
void GamingAI::SetMaxModelSize(size_t sizeInBytes) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamingAI::SetMaxModelSize() called - setting size to %zu bytes", sizeInBytes);
#endif

    // Use ThreadLockHelper for thread-safe model size update
    ThreadLockHelper configLock(threadManager, "gamingai_modelsize_set", 2000);
    if (!configLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock for model size update");
#endif
        return;                                                         // Failed to acquire lock
    }

    try {
        // Validate new model size
        size_t validatedSize = sizeInBytes;

        // Apply minimum and maximum limits
        const size_t MIN_MODEL_SIZE = 1024 * 1024;                     // 1MB minimum
        const size_t MAX_MODEL_SIZE = 2ULL * 1024 * 1024 * 1024;       // 2GB maximum

        if (validatedSize < MIN_MODEL_SIZE) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"Model size %zu too small, adjusting to minimum %zu bytes",
                validatedSize, MIN_MODEL_SIZE);
#endif
            validatedSize = MIN_MODEL_SIZE;                             // Set to minimum
        }
        else if (validatedSize > MAX_MODEL_SIZE) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"Model size %zu too large, adjusting to maximum %zu bytes",
                validatedSize, MAX_MODEL_SIZE);
#endif
            validatedSize = MAX_MODEL_SIZE;                             // Set to maximum
        }

        // Check if current model exceeds new size limit
        size_t currentModelSize = m_currentModelSize.load();
        if (currentModelSize > validatedSize) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"Current model size (%zu) exceeds new limit (%zu) - triggering cleanup",
                currentModelSize, validatedSize);
#endif

            // Inject cleanup command to reduce model size
            InjectAICommand(AICommandType::CMD_CLEAR_OUTDATED_DATA, AICommandPriority::PRIORITY_HIGH);
        }

        // Update configuration with new model size
        m_configuration.maxModelSizeBytes = validatedSize;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Max model size updated to %zu bytes (%.2f MB)",
            validatedSize, static_cast<float>(validatedSize) / (1024.0f * 1024.0f));
#endif
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception setting max model size: %S", e.what());
#endif
    }
}

// Set analysis interval
void GamingAI::SetAnalysisInterval(uint32_t seconds) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamingAI::SetAnalysisInterval() called - setting interval to %u seconds", seconds);
#endif

    // Use ThreadLockHelper for thread-safe interval update
    ThreadLockHelper configLock(threadManager, "gamingai_interval_set", 2000);
    if (!configLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock for analysis interval update");
#endif
        return;                                                         // Failed to acquire lock
    }

    try {
        // Validate new analysis interval
        uint32_t validatedInterval = seconds;

        // Apply minimum and maximum limits
        const uint32_t MIN_INTERVAL = 5;                               // 5 seconds minimum
        const uint32_t MAX_INTERVAL = 300;                             // 5 minutes maximum

        if (validatedInterval < MIN_INTERVAL) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"Analysis interval %u too short, adjusting to minimum %u seconds",
                validatedInterval, MIN_INTERVAL);
#endif
            validatedInterval = MIN_INTERVAL;                           // Set to minimum
        }
        else if (validatedInterval > MAX_INTERVAL) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"Analysis interval %u too long, adjusting to maximum %u seconds",
                validatedInterval, MAX_INTERVAL);
#endif
            validatedInterval = MAX_INTERVAL;                           // Set to maximum
        }

        // Update configuration with new analysis interval
        uint32_t previousInterval = m_configuration.analysisIntervalSeconds;
        m_configuration.analysisIntervalSeconds = validatedInterval;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Analysis interval updated from %u to %u seconds",
            previousInterval, validatedInterval);
#endif

        // If interval was shortened significantly, trigger immediate analysis
        if (validatedInterval < previousInterval / 2) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Interval shortened significantly - triggering immediate analysis");
#endif

            // Inject analysis command to adapt to new timing
            InjectAICommand(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT, AICommandPriority::PRIORITY_NORMAL);
        }
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception setting analysis interval: %S", e.what());
#endif
    }
}

//==============================================================================
// Advanced Configuration Management Methods
//==============================================================================

// Update specific configuration parameter by name (for external config systems)
bool GamingAI::UpdateConfigurationParameter(const std::string& parameterName, const std::string& parameterValue) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"GamingAI::UpdateConfigurationParameter() called - Parameter: %S, Value: %S",
        parameterName.c_str(), parameterValue.c_str());
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot update parameter - system not initialized");
#endif
        return false;                                                   // System not initialized
    }

    // Use ThreadLockHelper for thread-safe parameter update
    ThreadLockHelper configLock(threadManager, "gamingai_param_update", 2000);
    if (!configLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock for parameter update");
#endif
        return false;                                                   // Failed to acquire lock
    }

    try {
        // Process parameter updates based on parameter name
        if (parameterName == "maxModelSizeBytes") {
            size_t newSize = std::stoull(parameterValue);
            SetMaxModelSize(newSize);
            return true;
        }
        else if (parameterName == "analysisIntervalSeconds") {
            uint32_t newInterval = static_cast<uint32_t>(std::stoul(parameterValue));
            SetAnalysisInterval(newInterval);
            return true;
        }
        else if (parameterName == "dataRetentionDays") {
            float newRetention = std::stof(parameterValue);
            if (newRetention >= 1.0f && newRetention <= 365.0f) {
                m_configuration.dataRetentionDays = newRetention;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Data retention updated to %.1f days", newRetention);
#endif
                return true;
            }
        }
        else if (parameterName == "learningRate") {
            float newRate = std::stof(parameterValue);
            if (newRate >= 0.01f && newRate <= 1.0f) {
                m_configuration.learningRate = newRate;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Learning rate updated to %.3f", newRate);
#endif
                return true;
            }
        }
        else if (parameterName == "maxPlayerHistoryEntries") {
            uint32_t newEntries = static_cast<uint32_t>(std::stoul(parameterValue));
            if (newEntries >= 100 && newEntries <= 10000) {
                m_configuration.maxPlayerHistoryEntries = newEntries;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Max player history entries updated to %u", newEntries);
#endif
                return true;
            }
        }
        else if (parameterName == "enableAdvancedPrediction") {
            bool newValue = (parameterValue == "true" || parameterValue == "1");
            m_configuration.enableAdvancedPrediction = newValue;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Advanced prediction %s", newValue ? L"enabled" : L"disabled");
#endif
            return true;
        }
        else if (parameterName == "enableCrossSessionLearning") {
            bool newValue = (parameterValue == "true" || parameterValue == "1");
            m_configuration.enableCrossSessionLearning = newValue;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Cross-session learning %s", newValue ? L"enabled" : L"disabled");
#endif
            return true;
        }
        else if (parameterName == "enableRealTimeAnalysis") {
            bool newValue = (parameterValue == "true" || parameterValue == "1");
            m_configuration.enableRealTimeAnalysis = newValue;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Real-time analysis %s", newValue ? L"enabled" : L"disabled");
#endif
            return true;
        }
        else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Unknown configuration parameter: %S", parameterName.c_str());
#endif
            return false;                                               // Unknown parameter
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_WARNING,
            L"Parameter %S value %S out of valid range",
            parameterName.c_str(), parameterValue.c_str());
#endif
        return false;                                                   // Parameter value out of range
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION,
            L"Exception updating parameter %S: %S", parameterName.c_str(), e.what());
#endif
        return false;                                                   // Failed to update parameter
    }
}

// Export current configuration to string format (for saving to config files)
std::string GamingAI::ExportConfiguration() const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"GamingAI::ExportConfiguration() called - exporting current configuration");
#endif

    // Use ThreadLockHelper for thread-safe configuration export
    ThreadLockHelper configLock(threadManager, "gamingai_config_export", 1000, true); // Silent with timeout
    if (!configLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for config export - using cached data");
#endif
    }

    try {
        // Create configuration string in key=value format
        std::ostringstream configStream;

        configStream << "# GamingAI Configuration Export\n";
        configStream << "# Generated: " << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "\n\n";

        configStream << "maxModelSizeBytes=" << m_configuration.maxModelSizeBytes << "\n";
        configStream << "analysisIntervalSeconds=" << m_configuration.analysisIntervalSeconds << "\n";
        configStream << "dataRetentionDays=" << std::fixed << std::setprecision(1) << m_configuration.dataRetentionDays << "\n";
        configStream << "learningRate=" << std::fixed << std::setprecision(3) << m_configuration.learningRate << "\n";
        configStream << "maxPlayerHistoryEntries=" << m_configuration.maxPlayerHistoryEntries << "\n";
        configStream << "enableAdvancedPrediction=" << (m_configuration.enableAdvancedPrediction ? "true" : "false") << "\n";
        configStream << "enableCrossSessionLearning=" << (m_configuration.enableCrossSessionLearning ? "true" : "false") << "\n";
        configStream << "enableRealTimeAnalysis=" << (m_configuration.enableRealTimeAnalysis ? "true" : "false") << "\n";

        std::string configString = configStream.str();

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Configuration exported - %zu characters", configString.length());
#endif

        return configString;                                            // Return configuration string
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception exporting configuration: %S", e.what());
#endif
        return "";                                                      // Return empty string on error
    }
}

// Import configuration from string format (for loading from config files)
bool GamingAI::ImportConfiguration(const std::string& configString) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO,
        L"GamingAI::ImportConfiguration() called - importing configuration (%zu characters)",
        configString.length());
#endif

    try {
        // Parse configuration string line by line
        std::istringstream configStream(configString);
        std::string line;
        int parametersUpdated = 0;

        while (std::getline(configStream, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Find parameter=value separator
            size_t equalPos = line.find('=');
            if (equalPos == std::string::npos) {
                continue;                                               // Skip malformed lines
            }

            // Extract parameter name and value
            std::string paramName = line.substr(0, equalPos);
            std::string paramValue = line.substr(equalPos + 1);

            // Remove whitespace
            paramName.erase(0, paramName.find_first_not_of(" \t"));
            paramName.erase(paramName.find_last_not_of(" \t") + 1);
            paramValue.erase(0, paramValue.find_first_not_of(" \t"));
            paramValue.erase(paramValue.find_last_not_of(" \t") + 1);

            // Update parameter if valid
            if (UpdateConfigurationParameter(paramName, paramValue)) {
                parametersUpdated++;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"Updated parameter: %S = %S", paramName.c_str(), paramValue.c_str());
#endif
            }
            else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_WARNING,
                    L"Failed to update parameter: %S = %S", paramName.c_str(), paramValue.c_str());
#endif
            }
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Configuration import completed - %d parameters updated", parametersUpdated);
#endif

        return parametersUpdated > 0;                                   // Return success if any parameters updated
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception importing configuration: %S", e.what());
#endif
        return false;                                                   // Failed to import configuration
    }
}

//==============================================================================
// Model Persistence Methods Implementation
//==============================================================================

// Save current AI model to disk
bool GamingAI::SaveAIModel(const std::string& filename) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamingAI::SaveAIModel() called - saving model to: %S", filename.c_str());
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot save AI model - system not initialized");
#endif
        return false;                                                   // System not initialized
    }

    // Use specified filename or default
    std::string saveFilename = filename.empty() ? GetDefaultModelFilename() : filename;

    // Use ThreadLockHelper for thread-safe model saving
    ThreadLockHelper modelLock(threadManager, "gamingai_model_save", 10000); // Extended timeout for save operations
    if (!modelLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire model lock for saving - operation aborted");
#endif
        return false;                                                   // Failed to acquire lock
    }

    try {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Starting AI model save operation to: %S", saveFilename.c_str());
#endif

        // Save model using platform-appropriate method
        bool saveResult = SaveModelToDisk(saveFilename);

        if (saveResult) {
            // Update stored filename for future operations
            m_modelFilename = saveFilename;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"AI model saved successfully - File: %S, Size: %zu bytes",
                saveFilename.c_str(), m_currentModelSize.load());
#endif
        }
        else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to save AI model to: %S", saveFilename.c_str());
#endif
        }

        return saveResult;                                              // Return save operation result
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception saving AI model: %S", e.what());
#endif
        return false;                                                   // Failed to save model
    }
}

// Load AI model from disk
bool GamingAI::LoadAIModel(const std::string& filename) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamingAI::LoadAIModel() called - loading model from: %S", filename.c_str());
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot load AI model - system not initialized");
#endif
        return false;                                                   // System not initialized
    }

    // Use specified filename or default
    std::string loadFilename = filename.empty() ? GetDefaultModelFilename() : filename;

    // Check if model file exists before attempting load
    if (!ModelFileExists(loadFilename)) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"AI model file does not exist: %S", loadFilename.c_str());
#endif
        return false;                                                   // File doesn't exist
    }

    // Use ThreadLockHelper for thread-safe model loading
    ThreadLockHelper modelLock(threadManager, "gamingai_model_load", 10000); // Extended timeout for load operations
    if (!modelLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire model lock for loading - operation aborted");
#endif
        return false;                                                   // Failed to acquire lock
    }

    try {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Starting AI model load operation from: %S", loadFilename.c_str());
#endif

        // Load model using platform-appropriate method
        bool loadResult = LoadModelFromDisk(loadFilename);

        if (loadResult) {
            // Update stored filename for future operations
            m_modelFilename = loadFilename;

            // Validate loaded model data
            if (ValidateModelData()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO,
                    L"AI model loaded and validated successfully - File: %S, Size: %zu bytes",
                    loadFilename.c_str(), m_currentModelSize.load());
#endif
            }
            else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Loaded AI model failed validation - using with caution");
#endif
            }
        }
        else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to load AI model from: %S", loadFilename.c_str());
#endif
        }

        return loadResult;                                              // Return load operation result
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception loading AI model: %S", e.what());
#endif
        return false;                                                   // Failed to load model
    }
}

// Check if AI model file exists
bool GamingAI::ModelFileExists(const std::string& filename) const {
    // Use specified filename or default
    std::string checkFilename = filename.empty() ? GetDefaultModelFilename() : filename;

    try {
        // Platform-specific file existence check
#if defined(PLATFORM_WINDOWS)
    // Windows file existence check
        DWORD fileAttributes = GetFileAttributesA(checkFilename.c_str());
        bool exists = (fileAttributes != INVALID_FILE_ATTRIBUTES &&
            !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY));
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS) || defined(PLATFORM_ANDROID) || defined(PLATFORM_IOS)
    // POSIX file existence check
        struct stat fileStat;
        bool exists = (stat(checkFilename.c_str(), &fileStat) == 0 && S_ISREG(fileStat.st_mode));
#else
    // Fallback using standard C++ method
        std::ifstream file(checkFilename);
        bool exists = file.good();
        file.close();
#endif

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Model file existence check - File: %S, Exists: %s",
            checkFilename.c_str(), exists ? L"Yes" : L"No");
#endif

        return exists;                                                  // Return file existence status
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception checking model file existence: %S", e.what());
#endif
        return false;                                                   // Assume file doesn't exist on error
    }
}

// Clear all learned AI data and reset to defaults
void GamingAI::ResetAIModel() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamingAI::ResetAIModel() called - resetting AI model to defaults");
#endif

    // Use ThreadLockHelper for thread-safe model reset
    ThreadLockHelper modelLock(threadManager, "gamingai_model_reset", 5000);
    if (!modelLock.IsLocked()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire model lock for reset - operation aborted");
#endif
        return;                                                         // Failed to acquire lock
    }

    try {
        // Clear all player analysis data
        {
            ThreadLockHelper dataLock(threadManager, "gamingai_data_reset", 2000);
            if (dataLock.IsLocked()) {
                m_playerAnalysisData.clear();                           // Clear all player data

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Player analysis data cleared");
#endif
            }
        }

        // Reset current analysis results
        m_currentAnalysisResult = AIAnalysisResult();                   // Reset to default state
        m_currentStrategy = EnemyAIStrategy();                         // Reset strategy to defaults

        // Clear AI model data
        m_aiModelData.clear();                                          // Clear model data vector
        m_aiModelData.shrink_to_fit();                                  // Free allocated memory
        m_currentModelSize.store(0);                                   // Reset model size counter

        // Clear session tracking data
        m_sessionPlayerPositions.clear();                              // Clear position history
        m_sessionInputEvents.clear();                                  // Clear input history
        m_analysisTimings.clear();                                     // Clear timing history

        // Reserve memory for new data collection
        m_sessionPlayerPositions.reserve(10000);                       // Reserve space for positions
        m_sessionInputEvents.reserve(5000);                            // Reserve space for inputs
        m_analysisTimings.reserve(1000);                               // Reserve space for timings

        // Reset performance counters
        m_totalAnalysisCount.store(0);                                 // Reset analysis counter
        m_commandsProcessed.store(0);                                  // Reset command counter
        m_analysisReady.store(false);                                  // Mark analysis as not ready

        // Reset timing references
        m_performanceStartTime = std::chrono::steady_clock::now();     // Reset performance start time
        m_lastAnalysisTime = std::chrono::steady_clock::now();         // Reset last analysis time

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"AI model reset completed successfully - all data cleared");
#endif
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception resetting AI model: %S", e.what());
#endif
    }
}

//==============================================================================
// Private Model Persistence Implementation Methods
//==============================================================================

// Load AI model from disk storage (platform-specific implementation)
bool GamingAI::LoadModelFromDisk(const std::string& filename) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Loading AI model from disk: %S", filename.c_str());
#endif

    try {
        // Open model file for binary reading
        std::ifstream modelFile(filename, std::ios::binary | std::ios::ate);
        if (!modelFile.is_open()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to open model file for reading: %S", filename.c_str());
#endif
            return false;                                               // Failed to open file
        }

        // Get file size
        std::streamsize fileSize = modelFile.tellg();
        modelFile.seekg(0, std::ios::beg);

        // Validate file size
        if (fileSize <= 0 || static_cast<size_t>(fileSize) > m_configuration.maxModelSizeBytes) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"Invalid model file size: %lld bytes (max: %zu bytes)",
                static_cast<long long>(fileSize), m_configuration.maxModelSizeBytes);
#endif
            modelFile.close();
            return false;                                               // Invalid file size
        }

        // Read model header for validation
        struct ModelHeader {
            uint32_t magic;                                             // Magic number for validation
            uint32_t version;                                           // Model format version
            uint64_t dataSize;                                          // Size of model data
            uint32_t checksum;                                          // CRC32 checksum for integrity
            uint32_t compressionType;                                   // Compression method used
            char reserved[32];                                          // Reserved for future use
        } header;

        modelFile.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!modelFile.good()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read model header");
#endif
            modelFile.close();
            return false;                                               // Failed to read header
        }

        // Validate magic number
        const uint32_t EXPECTED_MAGIC = 0x41494D4F;                    // "AIMO" in hex
        if (header.magic != EXPECTED_MAGIC) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"Invalid model file magic number: 0x%08X (expected: 0x%08X)",
                header.magic, EXPECTED_MAGIC);
#endif
            modelFile.close();
            return false;                                               // Invalid magic number
        }

        // Validate version
        const uint32_t SUPPORTED_VERSION = 1;
        if (header.version > SUPPORTED_VERSION) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"Unsupported model version: %u (max supported: %u)",
                header.version, SUPPORTED_VERSION);
#endif
            modelFile.close();
            return false;                                               // Unsupported version
        }

        // Validate data size
        if (header.dataSize != static_cast<uint64_t>(fileSize - sizeof(header))) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"Model data size mismatch: header says %llu, file has %lld",
                header.dataSize, static_cast<long long>(fileSize - sizeof(header)));
#endif
            modelFile.close();
            return false;                                               // Size mismatch
        }

        // Read model data
        m_aiModelData.clear();
        m_aiModelData.resize(static_cast<size_t>(header.dataSize));

        modelFile.read(reinterpret_cast<char*>(m_aiModelData.data()), static_cast<std::streamsize>(header.dataSize));
        if (!modelFile.good()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read model data");
#endif
            modelFile.close();
            m_aiModelData.clear();
            return false;                                               // Failed to read data
        }

        modelFile.close();

        // Verify checksum using MathPrecalculation for fast CRC32
        uint32_t calculatedChecksum = FAST_MATH.FastFNV1aHash(m_aiModelData.data(), m_aiModelData.size());
        if (calculatedChecksum != header.checksum) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING,
                L"Model checksum mismatch: calculated 0x%08X, expected 0x%08X",
                calculatedChecksum, header.checksum);
#endif
            // Continue loading despite checksum mismatch (with warning)
        }

        // Decompress model data if needed
        if (header.compressionType != 0) {
            if (!DecompressModelData()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to decompress model data");
#endif
                m_aiModelData.clear();
                return false;                                           // Failed to decompress
            }
        }

        // Deserialize model data into analysis structures
        if (!DeserializeModelData()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to deserialize model data");
#endif
            m_aiModelData.clear();
            return false;                                               // Failed to deserialize
        }

        // Update model size tracking
        m_currentModelSize.store(m_aiModelData.size());

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"AI model loaded successfully - Size: %zu bytes, Version: %u, Players: %zu",
            m_aiModelData.size(), header.version, m_playerAnalysisData.size());
#endif

        return true;                                                    // Model loaded successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception loading model from disk: %S", e.what());
#endif
        m_aiModelData.clear();                                          // Clear data on error
        return false;                                                   // Failed to load model
    }
}

// Save AI model to disk storage (platform-specific implementation)
bool GamingAI::SaveModelToDisk(const std::string& filename) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Saving AI model to disk: %S", filename.c_str());
#endif

    try {
        // Serialize current analysis data into model format
        if (!SerializeModelData()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to serialize model data for saving");
#endif
            return false;                                               // Failed to serialize
        }

        // Compress model data if enabled and beneficial
        bool compressionUsed = false;
        if (m_aiModelData.size() > 1024 * 1024) {                      // Compress if larger than 1MB
            size_t originalSize = m_aiModelData.size();
            if (CompressModelData()) {
                compressionUsed = true;
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"Model data compressed - Original: %zu bytes, Compressed: %zu bytes",
                    originalSize, m_aiModelData.size());
#endif
            }
        }

        // Create model header
        struct ModelHeader {
            uint32_t magic;                                             // Magic number for validation
            uint32_t version;                                           // Model format version
            uint64_t dataSize;                                          // Size of model data
            uint32_t checksum;                                          // CRC32 checksum for integrity
            uint32_t compressionType;                                   // Compression method used
            char reserved[32];                                          // Reserved for future use
        } header;

        // Initialize header
        header.magic = 0x41494D4F;                                     // "AIMO" magic number
        header.version = 1;                                             // Current model version
        header.dataSize = static_cast<uint64_t>(m_aiModelData.size());
        header.checksum = FAST_MATH.FastFNV1aHash(m_aiModelData.data(), m_aiModelData.size()); // Calculate checksum
        header.compressionType = compressionUsed ? 1 : 0;              // Compression type indicator
        memset(header.reserved, 0, sizeof(header.reserved));           // Clear reserved space

        // Open file for binary writing
        std::ofstream modelFile(filename, std::ios::binary | std::ios::trunc);
        if (!modelFile.is_open()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to open model file for writing: %S", filename.c_str());
#endif
            return false;                                               // Failed to open file
        }

        // Write model header
        modelFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!modelFile.good()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to write model header");
#endif
            modelFile.close();
            return false;                                               // Failed to write header
        }

        // Write model data
        modelFile.write(reinterpret_cast<const char*>(m_aiModelData.data()),
            static_cast<std::streamsize>(m_aiModelData.size()));
        if (!modelFile.good()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to write model data");
#endif
            modelFile.close();
            return false;                                               // Failed to write data
        }

        modelFile.close();

        // Update model size tracking
        m_currentModelSize.store(m_aiModelData.size());

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"AI model saved successfully - File: %S, Size: %zu bytes, Compressed: %s",
            filename.c_str(), m_aiModelData.size(), compressionUsed ? L"Yes" : L"No");
#endif

        return true;                                                    // Model saved successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception saving model to disk: %S", e.what());
#endif
        return false;                                                   // Failed to save model
    }
}

// Get default AI model filename for current platform
std::string GamingAI::GetDefaultModelFilename() const {
    try {
        // Platform-specific default paths
#if defined(PLATFORM_WINDOWS)
    // Windows: Use Documents folder or current directory
        std::string defaultPath = "GamingAI_Model.dat";
#elif defined(PLATFORM_LINUX)
    // Linux: Use home directory or /tmp
        std::string defaultPath = "./GamingAI_Model.dat";
#elif defined(PLATFORM_MACOS)
    // macOS: Use Application Support directory
        std::string defaultPath = "./GamingAI_Model.dat";
#elif defined(PLATFORM_ANDROID)
    // Android: Use internal storage
        std::string defaultPath = "/data/data/gamingai/GamingAI_Model.dat";
#elif defined(PLATFORM_IOS)
    // iOS: Use Documents directory
        std::string defaultPath = "./GamingAI_Model.dat";
#else
    // Generic fallback
        std::string defaultPath = "GamingAI_Model.dat";
#endif

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Default model filename: %S", defaultPath.c_str());
#endif

        return defaultPath;                                             // Return platform-specific path
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception getting default model filename: %S", e.what());
#endif
        return "GamingAI_Model.dat";                                    // Return fallback filename
    }
}

//==============================================================================
// Main AI Thread Implementation (AIThreadTasking)
//==============================================================================

// Main AI thread processing function
void GamingAI::AIThreadTasking() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"AI thread started - beginning AI processing loop");
#endif

    // Set thread-specific initialization
    try {
        // Initialize thread-local performance monitoring
        auto threadStartTime = std::chrono::steady_clock::now();
        uint64_t totalCommandsProcessed = 0;
        uint64_t totalAnalysisOperations = 0;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"AI thread initialization completed - entering main processing loop");
#endif

        // Main AI processing loop - continues until shutdown requested
        while (!m_shouldShutdown.load()) {
            try {
                // Record loop iteration start time for performance monitoring
                auto loopStartTime = std::chrono::steady_clock::now();
                bool processedCommands = false;

                // Process all available commands in the queue
                {
                    ThreadLockHelper queueLock(threadManager, "gamingai_thread_queue", 1000, true); // Silent lock
                    if (queueLock.IsLocked()) {
                        // Process commands while queue is not empty
                        while (!m_commandQueue.empty() && !m_shouldShutdown.load()) {
                            // Get highest priority command from queue
                            AICommand currentCommand = m_commandQueue.top();
                            m_commandQueue.pop();

                            // Process the command
                            ProcessAICommand(currentCommand);
                            totalCommandsProcessed++;
                            processedCommands = true;

                            // Check for emergency shutdown command
                            if (currentCommand.commandType == AICommandType::CMD_EMERGENCY_SHUTDOWN) {
                                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Emergency shutdown command processed - terminating AI thread");
                                #endif
                                m_shouldShutdown.store(true);
                                break;                                  // Exit command processing loop
                            }

                            // Yield CPU if we've processed many commands continuously
                            if (totalCommandsProcessed % 50 == 0) {
                                std::this_thread::yield();             // Allow other threads to run
                            }
                        }
                    }
                }

                // Perform periodic analysis if interval has elapsed
                auto currentTime = std::chrono::steady_clock::now();
                auto timeSinceLastAnalysis = std::chrono::duration_cast<std::chrono::seconds>(
                    currentTime - m_lastAnalysisTime);

                if (timeSinceLastAnalysis.count() >= static_cast<long>(m_configuration.analysisIntervalSeconds)) {
                    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"Analysis interval elapsed (%lld seconds) - performing periodic analysis",
                            timeSinceLastAnalysis.count());
                    #endif

                    // Perform comprehensive periodic analysis
                    PerformPeriodicAnalysis();
                    totalAnalysisOperations++;

                    // Update last analysis time
                    m_lastAnalysisTime = currentTime;
                }

                // Calculate and log thread performance metrics periodically
                if (totalCommandsProcessed % 100 == 0 && totalCommandsProcessed > 0) {
                    auto threadRunTime = std::chrono::duration_cast<std::chrono::seconds>(
                        currentTime - threadStartTime);

                    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"AI thread performance - Runtime: %lld sec, Commands: %llu, Analysis ops: %llu",
                            threadRunTime.count(), totalCommandsProcessed, totalAnalysisOperations);
                    #endif
                }

                // Sleep briefly if no work was done to prevent CPU spinning
                if (!processedCommands && timeSinceLastAnalysis.count() < static_cast<long>(m_configuration.analysisIntervalSeconds)) {
                    // Use condition variable to wait for commands or timeout
                    std::unique_lock<std::mutex> lock(m_commandQueueMutex);
                    auto sleepDuration = std::chrono::milliseconds(500); // 500ms sleep when idle

                    m_commandAvailableCV.wait_for(lock, sleepDuration, [this] {
                        return !m_commandQueue.empty() || m_shouldShutdown.load();
                        });
                }

            }
            catch (const std::exception& e) {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception in AI thread main loop: %S", e.what());
                #endif

                // Continue operation unless it's a critical error
                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Brief pause before retry
            }
        }

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"AI thread shutdown completed - Processed %llu commands, %llu analysis operations",
                totalCommandsProcessed, totalAnalysisOperations);
        #endif

    }
    catch (const std::exception& e) 
    {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Fatal exception in AI thread: %S", e.what());
        #endif
    }

    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"AI thread terminated");
    #endif
}

// Perform periodic analysis operations
void GamingAI::PerformPeriodicAnalysis() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Performing periodic AI analysis");
    #endif

    try {
        // Record analysis start time for performance measurement
        auto analysisStartTime = std::chrono::steady_clock::now();

        // Only perform analysis if we have active players and are monitoring
        if (m_isMonitoring.load()) {
            std::vector<int> activePlayerIDs = gamePlayer.GetActivePlayerIDs();

            if (!activePlayerIDs.empty()) {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"Performing analysis for %zu active players", activePlayerIDs.size());
                #endif

                // Analyze each active player
                for (int playerID : activePlayerIDs) {
                    uint32_t playerId = static_cast<uint32_t>(playerID);

                    // Perform comprehensive player analysis
                    AnalyzePlayerMovement(playerId);
                    AnalyzePlayerCombat(playerId);
                    AnalyzePlayerInput(playerId);
                }

                // Generate strategic recommendations based on analysis
                GenerateEnemyStrategy();
                UpdateDifficultyRecommendations();

                // Periodically clean outdated data to manage memory usage
                static uint32_t cleanupCounter = 0;
                cleanupCounter++;
                if (cleanupCounter % 10 == 0) {                         // Clean every 10th analysis cycle
                    ClearOutdatedData();
                }

                // Mark analysis as ready for external consumption
                m_analysisReady.store(true);

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Periodic analysis completed successfully");
                #endif
            }
            else {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"No active players for analysis");
                #endif
            }
        }
        else {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Not monitoring - skipping periodic analysis");
            #endif
        }

        // Record analysis timing for performance monitoring
        auto analysisEndTime = std::chrono::steady_clock::now();
        auto analysisDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            analysisEndTime - analysisStartTime);

        // Store timing data with thread safety
        {
            ThreadLockHelper timingLock(threadManager, "gamingai_periodic_timing", 100, true);
            if (timingLock.IsLocked()) {
                m_analysisTimings.push_back(analysisDuration);

                // Maintain reasonable timing history size
                if (m_analysisTimings.size() > 1000) {
                    m_analysisTimings.erase(m_analysisTimings.begin(), m_analysisTimings.begin() + 200);
                }
            }
        }

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Periodic analysis timing: %lld ms", analysisDuration.count());
        #endif

    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception in periodic analysis: %S", e.what());
        #endif
    }
}

//==============================================================================
// Core Analysis Methods Implementation
//==============================================================================

// Analyze player movement patterns for specific player
void GamingAI::AnalyzePlayerMovement(uint32_t playerID) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Analyzing movement patterns for player %u", playerID);
    #endif

    try {
        // Get player information from GamePlayer system
        const PlayerInfo* playerInfo = gamePlayer.GetPlayerInfo(static_cast<int>(playerID));
        if (playerInfo == nullptr) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %u not found for movement analysis", playerID);
            #endif
            return;                                                     // Player not found
        }

        // Use ThreadLockHelper for thread-safe analysis data access
        ThreadLockHelper analysisLock(threadManager, "gamingai_movement_analysis", 2000);
        if (!analysisLock.IsLocked()) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for movement analysis");
            #endif
            return;                                                     // Failed to acquire lock
        }

        // Get or create player analysis data entry
        PlayerAnalysisData& playerData = m_playerAnalysisData[playerID];
        PlayerMovementPattern& movementData = playerData.movementData;

        // Calculate current velocity using MathPrecalculation for precision
        Vector2 currentPosition = playerInfo->position2D;
        Vector2 currentVelocity = playerInfo->velocity2D;

        // Update movement statistics if we have sufficient position history
        if (movementData.recentPositions.size() >= 2) {
            // Calculate average velocity over recent positions
            Vector2 totalVelocity(0.0f, 0.0f);
            uint32_t validSamples = 0;

            for (size_t i = 1; i < movementData.recentPositions.size(); ++i) {
                Vector2 deltaPos = Vector2(
                    movementData.recentPositions[i].x - movementData.recentPositions[i - 1].x,
                    movementData.recentPositions[i].y - movementData.recentPositions[i - 1].y
                );

                // Only include reasonable velocity changes (filter out teleports/glitches)
                float deltaDistance = FAST_MATH.FastSqrt(deltaPos.x * deltaPos.x + deltaPos.y * deltaPos.y);
                if (deltaDistance < 100.0f) {                           // Reasonable movement distance
                    totalVelocity.x += deltaPos.x;
                    totalVelocity.y += deltaPos.y;
                    validSamples++;
                }
            }

            if (validSamples > 0) {
                // Calculate average velocity using MathPrecalculation
                movementData.averageVelocity.x = totalVelocity.x / static_cast<float>(validSamples);
                movementData.averageVelocity.y = totalVelocity.y / static_cast<float>(validSamples);

                // Calculate movement predictability based on velocity consistency
                float velocityVariance = 0.0f;
                uint32_t varianceSamples = 0;

                for (size_t i = 1; i < movementData.recentPositions.size(); ++i) {
                    Vector2 deltaPos = Vector2(
                        movementData.recentPositions[i].x - movementData.recentPositions[i - 1].x,
                        movementData.recentPositions[i].y - movementData.recentPositions[i - 1].y
                    );

                    float deltaDistance = FAST_MATH.FastSqrt(deltaPos.x * deltaPos.x + deltaPos.y * deltaPos.y);
                    if (deltaDistance < 100.0f) {
                        Vector2 velocityDiff = Vector2(
                            deltaPos.x - movementData.averageVelocity.x,
                            deltaPos.y - movementData.averageVelocity.y
                        );

                        velocityVariance += velocityDiff.x * velocityDiff.x + velocityDiff.y * velocityDiff.y;
                        varianceSamples++;
                    }
                }

                if (varianceSamples > 0) {
                    velocityVariance /= static_cast<float>(varianceSamples);
                    // Convert variance to predictability (lower variance = higher predictability)
                    movementData.movementPredictability = 1.0f / (1.0f + velocityVariance * 0.1f);
                    movementData.movementPredictability = std::clamp(movementData.movementPredictability, 0.0f, 1.0f);
                }

                // Determine preferred movement direction using mathematical analysis
                if (FAST_MATH.FastSqrt(movementData.averageVelocity.x * movementData.averageVelocity.x +
                    movementData.averageVelocity.y * movementData.averageVelocity.y) > 0.1f) {
                    // Normalize preferred direction using MathPrecalculation
                    float velocityMagnitude = FAST_MATH.FastSqrt(
                        movementData.averageVelocity.x * movementData.averageVelocity.x +
                        movementData.averageVelocity.y * movementData.averageVelocity.y
                    );

                    if (velocityMagnitude > 0.0f) {
                        movementData.preferredDirection.x = movementData.averageVelocity.x / velocityMagnitude;
                        movementData.preferredDirection.y = movementData.averageVelocity.y / velocityMagnitude;
                    }
                }

                // Calculate aggressiveness factor based on movement speed and direction changes
                float averageSpeed = FAST_MATH.FastSqrt(
                    movementData.averageVelocity.x * movementData.averageVelocity.x +
                    movementData.averageVelocity.y * movementData.averageVelocity.y
                );

                // Higher speed and lower predictability indicate more aggressive movement
                movementData.aggressivenessFactor = (averageSpeed * 0.1f) + ((1.0f - movementData.movementPredictability) * 0.5f);
                movementData.aggressivenessFactor = std::clamp(movementData.aggressivenessFactor, 0.0f, 1.0f);
            }
        }

        // Update movement sample count
        movementData.totalMovementSamples++;

        // Estimate reaction time based on movement response to threats
        // This would typically involve analyzing response to enemy actions
        // For now, use a baseline calculation based on movement consistency
        if (movementData.movementPredictability > 0.8f) {
            movementData.reactionTime = 200.0f;                        // Fast, predictable movement
        }
        else if (movementData.movementPredictability > 0.5f) {
            movementData.reactionTime = 350.0f;                        // Average reaction time
        }
        else {
            movementData.reactionTime = 500.0f;                        // Slower, more erratic movement
        }

        // Update player skill level based on movement analysis
        playerData.skillLevel = CalculatePlayerSkillLevel(playerData);

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Movement analysis completed for player %u - Predictability: %.3f, Aggressiveness: %.3f, Skill: %u",
                playerID, movementData.movementPredictability, movementData.aggressivenessFactor, playerData.skillLevel);
        #endif

    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception analyzing player %u movement: %S", playerID, e.what());
        #endif
    }
}

// Analyze player combat behavior patterns
void GamingAI::AnalyzePlayerCombat(uint32_t playerID) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Analyzing combat patterns for player %u", playerID);
    #endif

    try {
        // Get player information from GamePlayer system
        const PlayerInfo* playerInfo = gamePlayer.GetPlayerInfo(static_cast<int>(playerID));
        if (playerInfo == nullptr) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %u not found for combat analysis", playerID);
            #endif
            return;                                                     // Player not found
        }

        // Use ThreadLockHelper for thread-safe analysis data access
        ThreadLockHelper analysisLock(threadManager, "gamingai_combat_analysis", 2000);
        if (!analysisLock.IsLocked()) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for combat analysis");
            #endif
            return;                                                     // Failed to acquire lock
        }

        // Get or create player analysis data entry
        PlayerAnalysisData& playerData = m_playerAnalysisData[playerID];
        PlayerCombatPattern& combatData = playerData.combatData;

        // Analyze current player combat status
        bool isInCombat = (playerInfo->currentState == PlayerState::ACTIVE &&
            playerInfo->health < playerInfo->maxHealth);

        // Calculate accuracy based on hit/miss ratio (simulated for now)
        // In a real implementation, this would track actual shots fired vs hits
        float baseAccuracy = 0.5f;                                     // Base accuracy assumption

        // Adjust accuracy based on player skill level and movement patterns
        if (playerData.movementData.movementPredictability > 0.7f) {
            baseAccuracy += 0.2f;                                      // Steady aim bonus
        }

        if (playerData.movementData.aggressivenessFactor > 0.8f) {
            baseAccuracy -= 0.1f;                                      // Aggressive movement penalty
        }

        // Apply skill level modifier using MathPrecalculation for smooth scaling
        float skillModifier = static_cast<float>(playerData.skillLevel) / 100.0f;
        combatData.accuracyPercentage = FAST_MATH.FastLerp(0.2f, 0.9f, skillModifier);
        combatData.accuracyPercentage = std::clamp(combatData.accuracyPercentage, 0.0f, 1.0f);

        // Calculate preferred engagement range based on player behavior
        // This would typically be based on distance analysis during combat encounters
        if (playerData.movementData.aggressivenessFactor > 0.7f) {
            combatData.preferredEngagementRange = 5.0f;                // Close-range aggressive player
        }
        else if (playerData.movementData.movementPredictability > 0.7f) {
            combatData.preferredEngagementRange = 15.0f;               // Mid-range tactical player
        }
        else {
            combatData.preferredEngagementRange = 25.0f;               // Long-range cautious player
        }

        // Calculate combat aggression based on movement and health management
        float healthRatio = static_cast<float>(playerInfo->health) / static_cast<float>(playerInfo->maxHealth);
        combatData.combatAggression = (playerData.movementData.aggressivenessFactor * 0.7f) +
            ((1.0f - healthRatio) * 0.3f);    // More aggressive when injured
        combatData.combatAggression = std::clamp(combatData.combatAggression, 0.0f, 1.0f);

        // Determine preferred combat position based on movement patterns
        combatData.preferredCombatPosition = playerData.movementData.preferredDirection;

        // Update combat engagement statistics
        if (isInCombat) {
            combatData.totalCombatEngagements++;

            // Determine if engagement was successful (simplified)
            if (playerInfo->health > playerInfo->maxHealth * 0.5f) {
                combatData.successfulEngagements++;
            }
        }

        // Calculate average combat duration (simulated)
        // In real implementation, this would track actual combat encounter durations
        float skillBasedDuration = FAST_MATH.FastLerp(8000.0f, 3000.0f, skillModifier); // 3-8 seconds based on skill
        combatData.averageCombatDuration = std::chrono::milliseconds(static_cast<long long>(skillBasedDuration));

        // Weapon switch frequency analysis (based on equipment changes)
        // This would track actual weapon switching in a real implementation
        combatData.weaponSwitchFrequency = static_cast<uint32_t>(playerData.skillLevel / 20); // More skilled = more weapon switching

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Combat analysis completed for player %u - Accuracy: %.3f, Aggression: %.3f, Range: %.1f",
                playerID, combatData.accuracyPercentage, combatData.combatAggression, combatData.preferredEngagementRange);
        #endif

    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception analyzing player %u combat: %S", playerID, e.what());
        #endif
    }
}

// Analyze player input patterns and timing
void GamingAI::AnalyzePlayerInput(uint32_t playerID) {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Analyzing input patterns for player %u", playerID);
    #endif

    try {
        // Use ThreadLockHelper for thread-safe analysis data access
        ThreadLockHelper analysisLock(threadManager, "gamingai_input_analysis", 2000);
        if (!analysisLock.IsLocked()) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for input analysis");
            #endif
            return;                                                     // Failed to acquire lock
        }

        // Get or create player analysis data entry
        PlayerAnalysisData& playerData = m_playerAnalysisData[playerID];
        PlayerInputPattern& inputData = playerData.inputData;

        // Analyze session input events to calculate actions per minute
        if (!m_sessionInputEvents.empty()) {
            auto sessionDuration = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - m_sessionStartTime);

            if (sessionDuration.count() > 0) {
                // Count different types of input events
                uint32_t keyboardEvents = 0;
                uint32_t mouseEvents = 0;
                uint32_t joystickEvents = 0;

                // Analyze encoded input events
                for (uint32_t encodedInput : m_sessionInputEvents) {
                    uint32_t inputType = (encodedInput >> 16) & 0xFFFF;

                    switch (inputType) {
                    case INPUT_TYPE_KEYBOARD:
                        keyboardEvents++;
                        break;
                    case INPUT_TYPE_MOUSE:
                        mouseEvents++;
                        break;
                    case INPUT_TYPE_JOYSTICK:
                        joystickEvents++;
                        break;
                    }
                }

                // Calculate actions per minute
                float minutesElapsed = static_cast<float>(sessionDuration.count());
                inputData.keyboardActionsPerMinute = static_cast<uint32_t>(keyboardEvents / minutesElapsed);
                inputData.mouseActionsPerMinute = static_cast<uint32_t>(mouseEvents / minutesElapsed);
                inputData.joystickActionsPerMinute = static_cast<uint32_t>(joystickEvents / minutesElapsed);

                // Calculate input consistency based on timing variance
                // Higher APM with consistent timing indicates skilled input patterns
                if (keyboardEvents + mouseEvents + joystickEvents > 10) {
                    float totalAPM = static_cast<float>(keyboardEvents + mouseEvents + joystickEvents) / minutesElapsed;

                    // Normalize consistency based on APM (higher APM generally means more consistent input)
                    inputData.inputConsistency = std::min(totalAPM / 200.0f, 1.0f); // 200 APM = perfect consistency
                }
                else {
                    inputData.inputConsistency = 0.1f;                  // Low consistency for minimal input
                }

                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"Input analysis for player %u - KB APM: %u, Mouse APM: %u, Consistency: %.3f",
                        playerID, inputData.keyboardActionsPerMinute, inputData.mouseActionsPerMinute, inputData.inputConsistency);
                #endif
            }
        }

        // Analyze input latency based on response times
        // This would typically measure time between input and game response
        // For now, estimate based on input consistency and session performance
        if (inputData.inputConsistency > 0.8f) {
            inputData.inputLatency = 25.0f;                             // Low latency for consistent input
        }
        else if (inputData.inputConsistency > 0.5f) {
            inputData.inputLatency = 50.0f;                             // Average latency
        }
        else {
            inputData.inputLatency = 100.0f;                            // Higher latency for inconsistent input
        }

        // Update mouse movement pattern analysis
        // This would track actual mouse movement in a real implementation
        Vector2 estimatedMouseMovement = playerData.movementData.averageVelocity;
        inputData.mouseMovementPattern = Vector2(
            estimatedMouseMovement.x * 0.1f,                           // Scale movement to mouse sensitivity
            estimatedMouseMovement.y * 0.1f
        );

    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception analyzing player %u input: %S", playerID, e.what());
        #endif
    }
}

//==============================================================================
// Strategic Analysis Methods Implementation
//==============================================================================

// Generate strategic recommendations based on analysis
void GamingAI::GenerateEnemyStrategy() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Generating enemy AI strategy based on player analysis");
    #endif

    try {
        // Use ThreadLockHelper for thread-safe strategy generation
        ThreadLockHelper strategyLock(threadManager, "gamingai_strategy_gen", 3000);
        if (!strategyLock.IsLocked()) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for strategy generation");
            #endif
            return;                                                     // Failed to acquire lock
        }

        // Initialize strategy with current timestamp
        EnemyAIStrategy newStrategy;
        newStrategy.strategyTimestamp = std::chrono::steady_clock::now();

        // Calculate overall player metrics for strategy determination
        float averageSkillLevel = 0.0f;
        float averageAggressiveness = 0.0f;
        float averagePredictability = 0.0f;
        float averageAccuracy = 0.0f;
        uint32_t validPlayerCount = 0;

        // Aggregate player statistics for strategy calculation
        for (const auto& playerPair : m_playerAnalysisData) {
            const PlayerAnalysisData& playerData = playerPair.second;

            if (ValidateAnalysisData(playerData)) {
                averageSkillLevel += static_cast<float>(playerData.skillLevel);
                averageAggressiveness += playerData.movementData.aggressivenessFactor;
                averagePredictability += playerData.movementData.movementPredictability;
                averageAccuracy += playerData.combatData.accuracyPercentage;
                validPlayerCount++;
            }
        }

        if (validPlayerCount == 0) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"No valid player data for strategy generation - using defaults");
            #endif

            // Use default strategy for no player data
            newStrategy.recommendedDifficulty = 0.5f;                  // Medium difficulty
            newStrategy.aggressionLevel = 0.5f;                       // Medium aggression
            newStrategy.tacticalIntelligence = 0.5f;                  // Medium intelligence
            newStrategy.recommendedEnemyCount = 3;                    // Default enemy count
        }
        else {
            // Calculate averages using MathPrecalculation for precision
            averageSkillLevel /= static_cast<float>(validPlayerCount);
            averageAggressiveness /= static_cast<float>(validPlayerCount);
            averagePredictability /= static_cast<float>(validPlayerCount);
            averageAccuracy /= static_cast<float>(validPlayerCount);

            // Normalize skill level to [0, 1] range
            float normalizedSkill = averageSkillLevel / 100.0f;

            // Calculate recommended difficulty using smooth curves
            // Higher skill = higher difficulty, but with diminishing returns
            newStrategy.recommendedDifficulty = FAST_MATH.FastSmoothStep(0.2f, 0.9f, normalizedSkill);

            // Adjust difficulty based on player behavior patterns
            if (averageAggressiveness > 0.8f) {
                // Very aggressive players can handle higher difficulty
                newStrategy.recommendedDifficulty = std::min(newStrategy.recommendedDifficulty + 0.1f, 1.0f);
            }
            else if (averagePredictability > 0.8f) {
                // Predictable players may need adaptive challenge
                newStrategy.recommendedDifficulty = FAST_MATH.FastLerp(newStrategy.recommendedDifficulty, 0.7f, 0.3f);
            }

            // Calculate AI aggression level to counter player behavior
            if (averageAggressiveness > 0.7f) {
                // Counter aggressive players with tactical AI
                newStrategy.aggressionLevel = FAST_MATH.FastLerp(0.3f, 0.6f, normalizedSkill);
            }
            else if (averagePredictability > 0.7f) {
                // Use unpredictable aggression against predictable players
                newStrategy.aggressionLevel = FAST_MATH.FastLerp(0.5f, 0.8f, normalizedSkill);
            }
            else {
                // Standard aggression scaling with skill
                newStrategy.aggressionLevel = FAST_MATH.FastLerp(0.4f, 0.7f, normalizedSkill);
            }

            // Calculate tactical intelligence based on player accuracy and skill
            float intelligenceBase = (normalizedSkill * 0.7f) + (averageAccuracy * 0.3f);
            newStrategy.tacticalIntelligence = FAST_MATH.FastSmoothStep(0.3f, 0.9f, intelligenceBase);

            // Determine optimal enemy count based on player performance
            if (averageSkillLevel > 80.0f && averageAccuracy > 0.8f) {
                newStrategy.recommendedEnemyCount = 6;                  // High skill = more enemies
            }
            else if (averageSkillLevel > 60.0f) {
                newStrategy.recommendedEnemyCount = 4;                  // Medium skill = moderate enemies
            }
            else if (averageSkillLevel > 30.0f) {
                newStrategy.recommendedEnemyCount = 3;                  // Low-medium skill = fewer enemies
            }
            else {
                newStrategy.recommendedEnemyCount = 2;                  // Low skill = minimal enemies
            }

            // Calculate optimal engagement range based on player combat preferences
            float totalPreferredRange = 0.0f;
            uint32_t rangeCount = 0;

            for (const auto& playerPair : m_playerAnalysisData) {
                const PlayerAnalysisData& playerData = playerPair.second;
                if (ValidateAnalysisData(playerData)) {
                    totalPreferredRange += playerData.combatData.preferredEngagementRange;
                    rangeCount++;
                }
            }

            if (rangeCount > 0) {
                float averagePlayerRange = totalPreferredRange / static_cast<float>(rangeCount);
                // AI should engage at range that challenges but doesn't overwhelm players
                newStrategy.engagementRange = averagePlayerRange * 1.2f; // 20% farther than player preference
            }
            else {
                newStrategy.engagementRange = 15.0f;                    // Default engagement range
            }
        }

        // Determine recommended positioning strategy
        if (averagePredictability > 0.7f) {
            // Counter predictable movement with flanking positions
            newStrategy.recommendedPositioning = Vector2(0.8f, 0.6f);  // Aggressive flanking
        }
        else if (averageAggressiveness > 0.7f) {
            // Counter aggressive players with defensive positions
            newStrategy.recommendedPositioning = Vector2(0.3f, 0.8f);  // Defensive positioning
        }
        else {
            // Balanced positioning for average players
            newStrategy.recommendedPositioning = Vector2(0.5f, 0.5f);  // Balanced positioning
        }

        // Generate tactical recommendations based on analysis
        newStrategy.recommendedTactics.clear();

        if (averageSkillLevel > 70.0f) {
            newStrategy.recommendedTactics.push_back("advanced_flanking");
            newStrategy.recommendedTactics.push_back("coordinated_attacks");
            newStrategy.recommendedTactics.push_back("predictive_movement");
        }
        else if (averageSkillLevel > 40.0f) {
            newStrategy.recommendedTactics.push_back("basic_flanking");
            newStrategy.recommendedTactics.push_back("pattern_variation");
            newStrategy.recommendedTactics.push_back("adaptive_timing");
        }
        else {
            newStrategy.recommendedTactics.push_back("direct_engagement");
            newStrategy.recommendedTactics.push_back("simple_patterns");
            newStrategy.recommendedTactics.push_back("fair_timing");
        }

        // Add behavioral counters based on player patterns
        if (averageAggressiveness > 0.8f) {
            newStrategy.recommendedTactics.push_back("defensive_counters");
            newStrategy.recommendedTactics.push_back("patience_tactics");
        }

        if (averagePredictability > 0.8f) {
            newStrategy.recommendedTactics.push_back("unpredictable_movement");
            newStrategy.recommendedTactics.push_back("pattern_breaking");
        }

        // Calculate prediction accuracy based on data quality and quantity
        float dataQuality = static_cast<float>(validPlayerCount) / 8.0f; // Assuming max 8 players
        uint64_t totalAnalysisOps = m_totalAnalysisCount.load();
        float experienceFactor = std::min(static_cast<float>(totalAnalysisOps) / 100.0f, 1.0f);

        newStrategy.predictionAccuracy = (dataQuality * 0.5f) + (experienceFactor * 0.5f);
        newStrategy.predictionAccuracy = std::clamp(newStrategy.predictionAccuracy, 0.3f, 0.95f);

        // Store the new strategy
        m_currentStrategy = newStrategy;

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Enemy strategy generated - Difficulty: %.3f, Aggression: %.3f, Intelligence: %.3f, Enemies: %u",
                newStrategy.recommendedDifficulty, newStrategy.aggressionLevel,
                newStrategy.tacticalIntelligence, newStrategy.recommendedEnemyCount);

            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Strategy details - Range: %.1f, Prediction accuracy: %.3f, Tactics: %zu",
                newStrategy.engagementRange, newStrategy.predictionAccuracy, newStrategy.recommendedTactics.size());
        #endif

    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception generating enemy strategy: %S", e.what());
        #endif
    }
}

// Update overall difficulty recommendations
void GamingAI::UpdateDifficultyRecommendations() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Updating difficulty recommendations based on player performance");
    #endif

    try {
        // Use ThreadLockHelper for thread-safe difficulty update
        ThreadLockHelper difficultyLock(threadManager, "gamingai_difficulty_update", 2000);
        if (!difficultyLock.IsLocked()) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for difficulty update");
            #endif
            return;                                                     // Failed to acquire lock
        }

        // Calculate adaptive difficulty based on current strategy and player performance
        float baseDifficulty = m_currentStrategy.recommendedDifficulty;

        // Adjust difficulty based on recent player performance trends
        float performanceTrend = CalculatePerformanceTrend();

        // Apply learning rate to difficulty adjustments for smooth transitions
        float adjustedDifficulty = FAST_MATH.FastLerp(baseDifficulty, baseDifficulty + performanceTrend,
            m_configuration.learningRate);

        // Clamp difficulty to reasonable bounds
        adjustedDifficulty = std::clamp(adjustedDifficulty, 0.1f, 0.95f);

        // Update current analysis result with new difficulty recommendation
        m_currentAnalysisResult.overallDifficultyRecommendation = adjustedDifficulty;

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Difficulty updated - Base: %.3f, Trend: %.3f, Final: %.3f",
                baseDifficulty, performanceTrend, adjustedDifficulty);
        #endif

    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception updating difficulty: %S", e.what());
        #endif
    }
}

// Clean up outdated analysis data
void GamingAI::ClearOutdatedData() {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Clearing outdated AI analysis data");
    #endif

    try {
        // Use ThreadLockHelper for thread-safe data cleanup
        ThreadLockHelper cleanupLock(threadManager, "gamingai_data_cleanup", 5000);
        if (!cleanupLock.IsLocked()) {
            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock for data cleanup");
            #endif
            return;                                                     // Failed to acquire lock
        }

        auto currentTime = std::chrono::system_clock::now();
        auto retentionDuration = std::chrono::hours(static_cast<long>(m_configuration.dataRetentionDays * 24));

        size_t playersRemoved = 0;
        size_t entriesCleared = 0;

        // Remove outdated player analysis data
        for (auto it = m_playerAnalysisData.begin(); it != m_playerAnalysisData.end();) {
            const PlayerAnalysisData& playerData = it->second;
            auto dataAge = currentTime - playerData.lastAnalysisTime;

            if (dataAge > retentionDuration) {
                #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG,
                        L"Removing outdated data for player %u (age: %lld hours)",
                        playerData.playerID, std::chrono::duration_cast<std::chrono::hours>(dataAge).count());
                #endif

                it = m_playerAnalysisData.erase(it);
                playersRemoved++;
            }
            else {
                // Clear excessive movement history to save memory
                PlayerAnalysisData& mutablePlayerData = it->second;
                if (mutablePlayerData.movementData.recentPositions.size() > m_configuration.maxPlayerHistoryEntries) {
                    size_t entriesToRemove = mutablePlayerData.movementData.recentPositions.size() -
                        (m_configuration.maxPlayerHistoryEntries / 2);

                    mutablePlayerData.movementData.recentPositions.erase(
                        mutablePlayerData.movementData.recentPositions.begin(),
                        mutablePlayerData.movementData.recentPositions.begin() + entriesToRemove);

                    entriesCleared += entriesToRemove;
                }
                ++it;
            }
        }

        // Trim analysis timing history to reasonable size
        if (m_analysisTimings.size() > 1000) {
            size_t timingsToRemove = m_analysisTimings.size() - 500;
            m_analysisTimings.erase(m_analysisTimings.begin(),
                m_analysisTimings.begin() + timingsToRemove);
            entriesCleared += timingsToRemove;
        }

        // Trim session data if it's getting too large
        if (m_sessionPlayerPositions.size() > 15000) {
            size_t positionsToRemove = m_sessionPlayerPositions.size() - 10000;
            m_sessionPlayerPositions.erase(m_sessionPlayerPositions.begin(),
                m_sessionPlayerPositions.begin() + positionsToRemove);
            entriesCleared += positionsToRemove;
        }

        if (m_sessionInputEvents.size() > 10000) {
            size_t eventsToRemove = m_sessionInputEvents.size() - 5000;
            m_sessionInputEvents.erase(m_sessionInputEvents.begin(),
                m_sessionInputEvents.begin() + eventsToRemove);
            entriesCleared += eventsToRemove;
        }

        // Update model size tracking
        size_t newModelSize = CalculateCurrentModelSize();
        m_currentModelSize.store(newModelSize);

        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Data cleanup completed - Players removed: %zu, Entries cleared: %zu, New model size: %zu bytes",
                playersRemoved, entriesCleared, newModelSize);
        #endif

    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception clearing outdated data: %S", e.what());
        #endif
    }
}

//==============================================================================
// Utility and Helper Methods Implementation
//==============================================================================

// Calculate player skill level based on performance metrics
uint32_t GamingAI::CalculatePlayerSkillLevel(const PlayerAnalysisData& playerData) const {
    try {
        // Weight different aspects of player performance
        const float MOVEMENT_WEIGHT = 0.3f;
        const float COMBAT_WEIGHT = 0.4f;
        const float INPUT_WEIGHT = 0.3f;

        // Calculate movement skill component
        float movementSkill = 0.0f;
        if (playerData.movementData.totalMovementSamples > 0) {
            // Higher predictability and appropriate aggression indicate skill
            movementSkill = (playerData.movementData.movementPredictability * 0.4f) +
                (std::min(playerData.movementData.aggressivenessFactor * 1.5f, 1.0f) * 0.4f) +
                (std::max(0.0f, 1.0f - (playerData.movementData.reactionTime / 1000.0f)) * 0.2f);
        }

        // Calculate combat skill component
        float combatSkill = 0.0f;
        if (playerData.combatData.totalCombatEngagements > 0) {
            float successRate = static_cast<float>(playerData.combatData.successfulEngagements) /
                static_cast<float>(playerData.combatData.totalCombatEngagements);

            combatSkill = (playerData.combatData.accuracyPercentage * 0.5f) +
                (successRate * 0.3f) +
                (std::min(playerData.combatData.combatAggression * 1.2f, 1.0f) * 0.2f);
        }

        // Calculate input skill component
        float inputSkill = 0.0f;
        uint32_t totalAPM = playerData.inputData.keyboardActionsPerMinute +
            playerData.inputData.mouseActionsPerMinute +
            playerData.inputData.joystickActionsPerMinute;

        if (totalAPM > 0) {
            // Normalize APM to skill range (200 APM = max skill for input)
            float apmSkill = std::min(static_cast<float>(totalAPM) / 200.0f, 1.0f);
            inputSkill = (apmSkill * 0.6f) + (playerData.inputData.inputConsistency * 0.4f);
        }

        // Combine skill components using weights
        float overallSkill = (movementSkill * MOVEMENT_WEIGHT) +
            (combatSkill * COMBAT_WEIGHT) +
            (inputSkill * INPUT_WEIGHT);

        // Apply experience modifier based on sessions analyzed
        float experienceModifier = 1.0f + (std::min(static_cast<float>(playerData.sessionsAnalyzed), 20.0f) * 0.025f);
        overallSkill *= experienceModifier;

        // Convert to skill level (1-100) using smooth scaling
        uint32_t skillLevel = static_cast<uint32_t>(FAST_MATH.FastSmoothStep(1.0f, 100.0f, overallSkill));
        skillLevel = std::clamp(skillLevel, 1U, 100U);

        return skillLevel;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception calculating skill level: %S", e.what());
#endif
        return 50;                                                      // Return average skill on error
    }
}

// Predict next player action based on historical patterns
Vector2 GamingAI::PredictPlayerNextAction(uint32_t playerID) const {
    try {
        auto playerIt = m_playerAnalysisData.find(playerID);
        if (playerIt == m_playerAnalysisData.end()) {
            return Vector2(0.0f, 0.0f);                                 // No data available
        }

        const PlayerAnalysisData& playerData = playerIt->second;
        const PlayerMovementPattern& movementData = playerData.movementData;

        // Use movement predictability to determine prediction confidence
        if (movementData.movementPredictability > 0.7f && movementData.recentPositions.size() >= 3) {
            // High predictability - use pattern extrapolation
            size_t posCount = movementData.recentPositions.size();
            Vector2 recentDirection = Vector2(
                movementData.recentPositions[posCount - 1].x - movementData.recentPositions[posCount - 2].x,
                movementData.recentPositions[posCount - 1].y - movementData.recentPositions[posCount - 2].y
            );

            // Apply predictability factor and preferred direction bias
            Vector2 predictedAction = Vector2(
                (recentDirection.x * movementData.movementPredictability) +
                (movementData.preferredDirection.x * (1.0f - movementData.movementPredictability)),
                (recentDirection.y * movementData.movementPredictability) +
                (movementData.preferredDirection.y * (1.0f - movementData.movementPredictability))
            );

            return predictedAction;
        }
        else if (movementData.totalMovementSamples > 10) {
            // Medium predictability - use average behavior
            return movementData.preferredDirection;
        }
        else {
            // Low predictability - return neutral prediction
            return Vector2(0.0f, 0.0f);
        }
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception predicting player action: %S", e.what());
#endif
        return Vector2(0.0f, 0.0f);                                     // Return neutral on error
    }
}

// Calculate adaptive difficulty adjustment
float GamingAI::CalculateAdaptiveDifficulty(const PlayerAnalysisData& playerData) const {
    try {
        // Base difficulty on player skill level
        float baseDifficulty = static_cast<float>(playerData.skillLevel) / 100.0f;

        // Adjust for player behavior patterns
        if (playerData.movementData.aggressivenessFactor > 0.8f) {
            baseDifficulty += 0.1f;                                     // Aggressive players can handle more
        }

        if (playerData.combatData.accuracyPercentage > 0.8f) {
            baseDifficulty += 0.15f;                                    // Accurate players need more challenge
        }

        if (playerData.inputData.inputConsistency > 0.8f) {
            baseDifficulty += 0.05f;                                    // Consistent input indicates skill
        }

        // Adjust based on recent performance (if available)
        if (playerData.combatData.totalCombatEngagements > 5) {
            float recentSuccessRate = static_cast<float>(playerData.combatData.successfulEngagements) /
                static_cast<float>(playerData.combatData.totalCombatEngagements);

            if (recentSuccessRate > 0.8f) {
                baseDifficulty += 0.1f;                                 // High success rate = increase difficulty
            }
            else if (recentSuccessRate < 0.3f) {
                baseDifficulty -= 0.1f;                                 // Low success rate = decrease difficulty
            }
        }

        // Apply learning rate for smooth transitions
        return std::clamp(baseDifficulty, 0.1f, 0.95f);
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception calculating adaptive difficulty: %S", e.what());
#endif
        return 0.5f;                                                    // Return medium difficulty on error
    }
}

//==============================================================================
// Performance and Statistics Helper Methods
//==============================================================================

// Calculate performance trend for difficulty adjustment
float GamingAI::CalculatePerformanceTrend() const {
    try {
        // Calculate average performance change over recent sessions
        float totalTrend = 0.0f;
        uint32_t trendSamples = 0;

        for (const auto& playerPair : m_playerAnalysisData) {
            const PlayerAnalysisData& playerData = playerPair.second;

            if (playerData.sessionsAnalyzed >= 3) {
                // Estimate performance trend based on combat success and skill
                float currentPerformance = static_cast<float>(playerData.skillLevel) / 100.0f;

                if (playerData.combatData.totalCombatEngagements > 0) {
                    float successRate = static_cast<float>(playerData.combatData.successfulEngagements) /
                        static_cast<float>(playerData.combatData.totalCombatEngagements);
                    currentPerformance = (currentPerformance + successRate) * 0.5f;
                }

                // Simple trend calculation (would be more sophisticated with historical data)
                float trend = (currentPerformance - 0.5f) * 0.2f;      // Scale trend impact
                totalTrend += trend;
                trendSamples++;
            }
        }

        if (trendSamples > 0) {
            return totalTrend / static_cast<float>(trendSamples);
        }

        return 0.0f;                                                    // No trend data available
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception calculating performance trend: %S", e.what());
#endif
        return 0.0f;                                                    // Return neutral trend on error
    }
}

// Calculate current model size for memory management
size_t GamingAI::CalculateCurrentModelSize() const {
    try {
        size_t totalSize = 0;

        // Calculate size of player analysis data
        totalSize += m_playerAnalysisData.size() * sizeof(PlayerAnalysisData);

        // Add size of movement history for each player
        for (const auto& playerPair : m_playerAnalysisData) {
            const PlayerAnalysisData& playerData = playerPair.second;
            totalSize += playerData.movementData.recentPositions.size() * sizeof(Vector2);
        }

        // Add size of session data
        totalSize += m_sessionPlayerPositions.size() * sizeof(Vector2);
        totalSize += m_sessionInputEvents.size() * sizeof(uint32_t);
        totalSize += m_analysisTimings.size() * sizeof(std::chrono::milliseconds);

        // Add size of AI model data
        totalSize += m_aiModelData.size();

        // Add overhead estimates for containers and structures
        totalSize += m_playerAnalysisData.size() * 64;                  // Container overhead estimate

        return totalSize;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception calculating model size: %S", e.what());
#endif
        return m_currentModelSize.load();                               // Return last known size on error
    }
}

//==============================================================================
// Model Data Serialization Methods (Implementation Stubs)
//==============================================================================

// Serialize current analysis data into model format
bool GamingAI::SerializeModelData() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Serializing AI model data for storage");
#endif

    try {
        // Clear existing model data
        m_aiModelData.clear();

        // Reserve space for serialized data
        m_aiModelData.reserve(1024 * 1024);                             // Reserve 1MB initially

        // Serialize player analysis data (simplified binary format)
        uint32_t playerCount = static_cast<uint32_t>(m_playerAnalysisData.size());

        // Write player count
        m_aiModelData.insert(m_aiModelData.end(),
            reinterpret_cast<uint8_t*>(&playerCount),
            reinterpret_cast<uint8_t*>(&playerCount) + sizeof(playerCount));

        // Serialize each player's data
        for (const auto& playerPair : m_playerAnalysisData) {
            const PlayerAnalysisData& playerData = playerPair.second;

            // Serialize basic player info (simplified)
            m_aiModelData.insert(m_aiModelData.end(),
                reinterpret_cast<const uint8_t*>(&playerData),
                reinterpret_cast<const uint8_t*>(&playerData) + sizeof(PlayerAnalysisData));
        }

        // Update model size
        m_currentModelSize.store(m_aiModelData.size());

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Model data serialized - Size: %zu bytes, Players: %u",
            m_aiModelData.size(), playerCount);
#endif

        return true;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception serializing model data: %S", e.what());
#endif
        m_aiModelData.clear();                                          // Clear data on error
        return false;                                                   // Failed to serialize
    }
}

// Deserialize model data into analysis structures
bool GamingAI::DeserializeModelData() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Deserializing AI model data from storage");
#endif

    try {
        if (m_aiModelData.empty()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"No model data available for deserialization");
#endif
            return false;                                               // No data to deserialize
        }

        // Clear existing analysis data
        m_playerAnalysisData.clear();

        // Read player count
        if (m_aiModelData.size() < sizeof(uint32_t)) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Model data too small for player count");
#endif
            return false;                                               // Insufficient data
        }

        uint32_t playerCount;
        memcpy(&playerCount, m_aiModelData.data(), sizeof(playerCount));
        size_t offset = sizeof(playerCount);

        // Validate player count
        if (playerCount > 100) {                                        // Reasonable limit
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid player count in model data: %u", playerCount);
#endif
            return false;                                               // Invalid player count
        }

        // Deserialize each player's data
        for (uint32_t i = 0; i < playerCount; ++i) {
            if (offset + sizeof(PlayerAnalysisData) > m_aiModelData.size()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR,
                    L"Insufficient data for player %u (offset: %zu, total: %zu)",
                    i, offset, m_aiModelData.size());
#endif
                return false;                                           // Insufficient data
            }

            PlayerAnalysisData playerData;
            memcpy(&playerData, m_aiModelData.data() + offset, sizeof(PlayerAnalysisData));
            offset += sizeof(PlayerAnalysisData);

            // Validate player data
            if (ValidateAnalysisData(playerData)) {
                m_playerAnalysisData[playerData.playerID] = playerData;

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                    L"Deserialized player %u data - Skill: %u, Sessions: %u",
                    playerData.playerID, playerData.skillLevel, playerData.sessionsAnalyzed);
#endif
            }
            else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_WARNING,
                    L"Player %u data failed validation - skipping", playerData.playerID);
#endif
            }
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Model data deserialized successfully - Players loaded: %zu", m_playerAnalysisData.size());
#endif

        return true;                                                    // Deserialization successful
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception deserializing model data: %S", e.what());
#endif
        m_playerAnalysisData.clear();                                   // Clear data on error
        return false;                                                   // Failed to deserialize
    }
}

// Validate AI model data integrity
bool GamingAI::ValidateModelData() const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Validating AI model data integrity");
#endif

    try {
        // Check if model data exists
        if (m_aiModelData.empty()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"No model data to validate");
#endif
            return false;                                               // No data to validate
        }

        // Check model size constraints
        if (m_aiModelData.size() > m_configuration.maxModelSizeBytes) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"Model data exceeds size limit: %zu > %zu bytes",
                m_aiModelData.size(), m_configuration.maxModelSizeBytes);
#endif
            return false;                                               // Model too large
        }

        // Validate minimum data size
        if (m_aiModelData.size() < sizeof(uint32_t)) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Model data too small for basic structure");
#endif
            return false;                                               // Data too small
        }

        // Validate player analysis data
        size_t validPlayers = 0;
        size_t invalidPlayers = 0;

        for (const auto& playerPair : m_playerAnalysisData) {
            if (ValidateAnalysisData(playerPair.second)) {
                validPlayers++;
            }
            else {
                invalidPlayers++;
            }
        }

        // Require at least some valid data
        if (validPlayers == 0 && !m_playerAnalysisData.empty()) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"No valid player analysis data found in model");
#endif
            return false;                                               // No valid data
        }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Model validation completed - Valid players: %zu, Invalid: %zu, Size: %zu bytes",
            validPlayers, invalidPlayers, m_aiModelData.size());
#endif

        return true;                                                    // Model validation passed
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception validating model data: %S", e.what());
#endif
        return false;                                                   // Failed validation
    }
}

// Compress model data for storage efficiency
bool GamingAI::CompressModelData() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Compressing AI model data for storage");
#endif

    try {
        if (m_aiModelData.empty()) {
            return true;                                                // Nothing to compress
        }

        size_t originalSize = m_aiModelData.size();

        // Simple compression placeholder - in a real implementation, you would use
        // a compression library like zlib, lz4, or integrate with PUNPack if available

        // For now, we'll implement a basic run-length encoding for demonstration
        std::vector<uint8_t> compressedData;
        compressedData.reserve(originalSize);                           // Reserve space

        if (originalSize > 0) {
            uint8_t currentByte = m_aiModelData[0];
            uint8_t count = 1;

            for (size_t i = 1; i < originalSize; ++i) {
                if (m_aiModelData[i] == currentByte && count < 255) {
                    count++;
                }
                else {
                    // Write count and byte
                    compressedData.push_back(count);
                    compressedData.push_back(currentByte);

                    currentByte = m_aiModelData[i];
                    count = 1;
                }
            }

            // Write final count and byte
            compressedData.push_back(count);
            compressedData.push_back(currentByte);
        }

        // Only use compression if it actually reduces size
        if (compressedData.size() < originalSize) {
            m_aiModelData = std::move(compressedData);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Model data compressed - Original: %zu bytes, Compressed: %zu bytes (%.1f%% reduction)",
                originalSize, m_aiModelData.size(),
                ((float)(originalSize - m_aiModelData.size()) / (float)originalSize) * 100.0f);
#endif

            return true;                                                // Compression successful
        }
        else {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Compression did not reduce size - keeping original data");
#endif

            return false;                                               // Compression not beneficial
        }
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception compressing model data: %S", e.what());
#endif
        return false;                                                   // Failed to compress
    }
}

// Decompress model data after loading
bool GamingAI::DecompressModelData() {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Decompressing AI model data after loading");
#endif

    try {
        if (m_aiModelData.empty()) {
            return true;                                                // Nothing to decompress
        }

        size_t compressedSize = m_aiModelData.size();

        // Decompress using simple run-length decoding (matching compression above)
        std::vector<uint8_t> decompressedData;
        decompressedData.reserve(compressedSize * 2);                   // Estimate decompressed size

        for (size_t i = 0; i < compressedSize; i += 2) {
            if (i + 1 < compressedSize) {
                uint8_t count = m_aiModelData[i];
                uint8_t byte = m_aiModelData[i + 1];

                // Add repeated bytes to decompressed data
                for (uint8_t j = 0; j < count; ++j) {
                    decompressedData.push_back(byte);
                }
            }
        }

        // Replace compressed data with decompressed data
        m_aiModelData = std::move(decompressedData);

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"Model data decompressed - Compressed: %zu bytes, Decompressed: %zu bytes",
            compressedSize, m_aiModelData.size());
#endif

        return true;                                                    // Decompression successful
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception decompressing model data: %S", e.what());
#endif
        return false;                                                   // Failed to decompress
    }
}

//==============================================================================
// High-Performance Optimized Methods (Assembly when beneficial)
//==============================================================================

// Optimized vector distance calculation using SIMD when available
float GamingAI::FastVectorDistance(const Vector2& pos1, const Vector2& pos2) const {
    try {
        // Calculate distance using MathPrecalculation for optimal performance
        float dx = pos2.x - pos1.x;
        float dy = pos2.y - pos1.y;

        // Use fast square root from MathPrecalculation
        return FAST_MATH.FastSqrt(dx * dx + dy * dy);
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in fast vector distance: %S", e.what());
#endif
        return 0.0f;                                                    // Return 0 on error
    }
}

// Optimized pattern matching algorithm
float GamingAI::FastPatternMatch(const std::vector<Vector2>& pattern1, const std::vector<Vector2>& pattern2) const {
    try {
        if (pattern1.empty() || pattern2.empty()) {
            return 0.0f;                                                // No patterns to match
        }

        size_t minSize = std::min(pattern1.size(), pattern2.size());
        if (minSize == 0) {
            return 0.0f;                                                // No valid comparison size
        }

        float totalSimilarity = 0.0f;

        // Compare corresponding points in both patterns
        for (size_t i = 0; i < minSize; ++i) {
            float distance = FastVectorDistance(pattern1[i], pattern2[i]);

            // Convert distance to similarity (closer = more similar)
            float similarity = 1.0f / (1.0f + distance);
            totalSimilarity += similarity;
        }

        // Return average similarity
        return totalSimilarity / static_cast<float>(minSize);
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in fast pattern match: %S", e.what());
#endif
        return 0.0f;                                                    // Return 0 on error
    }
}

// Optimized data correlation analysis
float GamingAI::FastCorrelationAnalysis(const float* data1, const float* data2, size_t dataCount) const {
    try {
        if (data1 == nullptr || data2 == nullptr || dataCount == 0) {
            return 0.0f;                                                // Invalid input parameters
        }

        // Calculate means
        float mean1 = 0.0f, mean2 = 0.0f;
        for (size_t i = 0; i < dataCount; ++i) {
            mean1 += data1[i];
            mean2 += data2[i];
        }
        mean1 /= static_cast<float>(dataCount);
        mean2 /= static_cast<float>(dataCount);

        // Calculate correlation coefficient
        float numerator = 0.0f;
        float sum1Sq = 0.0f, sum2Sq = 0.0f;

        for (size_t i = 0; i < dataCount; ++i) {
            float diff1 = data1[i] - mean1;
            float diff2 = data2[i] - mean2;

            numerator += diff1 * diff2;
            sum1Sq += diff1 * diff1;
            sum2Sq += diff2 * diff2;
        }

        // Calculate correlation coefficient using MathPrecalculation
        float denominator = FAST_MATH.FastSqrt(sum1Sq * sum2Sq);

        if (denominator > 1e-8f) {
            return numerator / denominator;                             // Return correlation coefficient
        }
        else {
            return 0.0f;                                                // No correlation (zero variance)
        }
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Exception in fast correlation analysis: %S", e.what());
#endif
        return 0.0f;                                                    // Return 0 on error
    }
}

//==============================================================================
// Assembly Optimized Methods (Platform-Specific)
//==============================================================================
//==============================================================================
// High-Performance Optimized Methods (Fixed for x64 Visual Studio)
//==============================================================================

#if defined(PLATFORM_WINDOWS) && defined(_M_X64)
// High-performance memory copy for large data sets using MSVC intrinsics
void GamingAI::FastMemoryCopy(void* dest, const void* src, size_t size) const {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Performing fast memory copy - Size: %zu bytes", size);
#endif

    try {
        // Use optimized intrinsics for large copies (>1KB)
        if (size > 1024) {
            // Use MSVC intrinsics instead of inline assembly for x64 compatibility
#include <intrin.h>

// Ensure 16-byte alignment for optimal SIMD performance
            const size_t SIMD_ALIGNMENT = 16;
            uint8_t* destPtr = static_cast<uint8_t*>(dest);
            const uint8_t* srcPtr = static_cast<const uint8_t*>(src);

            // Handle unaligned start
            while (((uintptr_t)destPtr & (SIMD_ALIGNMENT - 1)) && size > 0) {
                *destPtr++ = *srcPtr++;                                // Copy byte by byte until aligned
                size--;
            }

            // Perform 16-byte aligned copies using SSE2 intrinsics
            while (size >= 16) {
                __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(srcPtr)); // Load 16 bytes
                _mm_store_si128(reinterpret_cast<__m128i*>(destPtr), data);              // Store 16 bytes
                srcPtr += 16;                                           // Advance source pointer
                destPtr += 16;                                          // Advance destination pointer
                size -= 16;                                             // Decrease remaining size
            }

            // Copy remaining bytes
            while (size > 0) {
                *destPtr++ = *srcPtr++;                                // Copy remaining bytes
                size--;
            }

#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"SSE2-optimized memory copy completed");
#endif
        }
        else {
            // Use standard copy for small sizes (compiler will optimize)
            memcpy(dest, src, size);                                   // Standard library for small copies
        }
    }
    catch (...) {
#if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Exception in optimized memory copy - falling back to memcpy");
#endif

        // Fallback to standard memory copy on any error
        memcpy(dest, src, size);                                       // Safe fallback implementation
    }
}
#else
// Fallback implementation for non-Windows or non-x64 platforms
void GamingAI::FastMemoryCopy(void* dest, const void* src, size_t size) const {
    // Use standard library function on unsupported platforms
    memcpy(dest, src, size);                                           // Cross-platform compatibility
}
#endif

// High-performance data processing using SSE2 intrinsics
#if defined(PLATFORM_WINDOWS) && defined(_M_X64)
uint32_t GamingAI::FastDataChecksum(const uint8_t* data, size_t size) const {
    #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Calculating fast checksum - Size: %zu bytes", size);
    #endif

    try {
        uint32_t checksum = 0;

        if (size >= 16) {
            const uint8_t* dataPtr = data;                             // Pointer to current data position
            size_t remainingSize = size;                               // Track remaining bytes to process

            // Process 16 bytes at a time using SSE2 intrinsics
            while (remainingSize >= 16) {
                __m128i dataChunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dataPtr)); // Load 16 bytes

                // Sum bytes using horizontal add operations
                __m128i zero = _mm_setzero_si128();                    // Create zero vector for SAD operation
                __m128i sad = _mm_sad_epu8(dataChunk, zero);           // Sum of absolute differences (byte sum)

                // Extract the sum from the SSE register
                uint32_t chunkSum = static_cast<uint32_t>(_mm_cvtsi128_si32(sad)); // Extract low 32 bits
                checksum += chunkSum;                                  // Add to running checksum

                dataPtr += 16;                                         // Advance data pointer
                remainingSize -= 16;                                   // Decrease remaining size
            }

            // Process remaining bytes with standard loop
            while (remainingSize > 0) {
                checksum += *dataPtr++;                                // Add each remaining byte
                remainingSize--;                                       // Decrease counter
            }

            #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"SSE2-optimized checksum calculated: 0x%08X", checksum);
            #endif
        }
        else {
            // Process small data sizes with simple loop
            for (size_t i = 0; i < size; ++i) {
                checksum += data[i];                                   // Simple byte summation for small data
            }
        }

        return checksum;                                               // Return calculated checksum
    }
    catch (...) {
        #if defined(_DEBUG_GAMINGAI_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Exception in optimized checksum - using fallback");
        #endif

        // Fallback to MathPrecalculation hash function on error
        return FAST_MATH.FastFNV1aHash(data, size);                   // Safe fallback using existing hash
    }
}
#else
// Fallback implementation for non-Windows or non-x64 platforms
uint32_t GamingAI::FastDataChecksum(const uint8_t* data, size_t size) const {
    // Use MathPrecalculation hash function on unsupported platforms
    return FAST_MATH.FastFNV1aHash(data, size);                       // Cross-platform compatibility
}
#endif

#pragma warning(pop)
