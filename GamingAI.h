// -------------------------------------------------------------------------------------------------------------
// GamingAI.h - Comprehensive Gaming AI Intelligence System
// 
// This class provides advanced AI analysis of player behavior, movement patterns, and strategic decision making
// to enhance enemy AI capabilities and provide dynamic difficulty adjustment based on player skill assessment.
// Supports cross-platform operation with optimized performance and thread-safe data collection.
// -------------------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#include "Debug.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"
#include "MathPrecalculation.h"
#include "GamePlayer.h"
#include "Joystick.h"
#include "Vectors.h"

#include <vector>
#include <queue>
#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <fstream>
#include <unordered_map>
#include <functional>

// Forward declarations
extern Debug debug;
extern ThreadManager threadManager;
extern GamePlayer gamePlayer;

#if defined(__USING_JOYSTICKS__)
    extern Joystick js;
#endif

//==============================================================================
// Platform-specific conditional compilation blocks
//==============================================================================
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
    #include <windows.h>
#elif defined(__linux__)
    #define PLATFORM_LINUX
    #include <unistd.h>
    #include <sys/stat.h>
#elif defined(__APPLE__)
    #define PLATFORM_MACOS
    #include <unistd.h>
    #include <sys/stat.h>
#elif defined(__ANDROID__)
    #define PLATFORM_ANDROID
    #include <unistd.h>
    #include <sys/stat.h>
#elif defined(__iOS__)
    #define PLATFORM_IOS
    #include <unistd.h>
    #include <sys/stat.h>
#endif

// Input type constants for data collection
const uint32_t INPUT_TYPE_KEYBOARD = 1;
const uint32_t INPUT_TYPE_MOUSE = 2;
const uint32_t INPUT_TYPE_JOYSTICK = 3;

//==============================================================================
// AI Command System - Priority-based command queue for AI operations
//==============================================================================
enum class AICommandType : uint32_t {
    CMD_ANALYZE_PLAYER_MOVEMENT = 0x00000001,                           // Analyze current player movement patterns
    CMD_ANALYZE_PLAYER_COMBAT = 0x00000002,                             // Analyze player combat behavior and tactics
    CMD_ANALYZE_PLAYER_STRATEGY = 0x00000004,                           // Analyze overall player strategic decisions
    CMD_UPDATE_DIFFICULTY = 0x00000008,                                 // Update AI difficulty based on player skill
    CMD_SAVE_AI_MODEL = 0x00000010,                                     // Save current AI learning model to disk
    CMD_LOAD_AI_MODEL = 0x00000020,                                     // Load AI learning model from disk
    CMD_CLEAR_OUTDATED_DATA = 0x00000040,                               // Remove outdated player behavior data
    CMD_GENERATE_ENEMY_STRATEGY = 0x00000080,                           // Generate new enemy behavior strategies
    CMD_ANALYZE_INPUT_PATTERNS = 0x00000100,                            // Analyze keyboard/mouse/joystick input patterns
    CMD_PREDICT_PLAYER_ACTION = 0x00000200,                             // Predict next likely player action
    CMD_EMERGENCY_SHUTDOWN = 0x80000000                                 // Emergency AI system shutdown
};

// AI Command Priority Levels - Higher values = Higher priority
enum class AICommandPriority : uint8_t {
    PRIORITY_LOW = 1,                                                   // Low priority background tasks
    PRIORITY_NORMAL = 5,                                                // Normal analysis operations
    PRIORITY_HIGH = 8,                                                  // Important strategic analysis
    PRIORITY_CRITICAL = 10,                                             // Critical system operations
    PRIORITY_EMERGENCY = 15                                             // Emergency shutdown or critical errors
};

// AI Command Structure for queue management
struct AICommand {
    AICommandType commandType;                                          // Type of AI command to execute
    AICommandPriority priority;                                         // Command execution priority
    std::chrono::steady_clock::time_point timestamp;                    // When command was issued
    std::string commandData;                                            // Additional command-specific data
    uint32_t playerID;                                                  // Target player ID for command
    bool requiresImmediate;                                             // Whether command needs immediate processing

    // Constructor with default initialization
    AICommand() :
        commandType(AICommandType::CMD_ANALYZE_PLAYER_MOVEMENT),
        priority(AICommandPriority::PRIORITY_NORMAL),
        timestamp(std::chrono::steady_clock::now()),
        commandData(""),
        playerID(0),
        requiresImmediate(false)
    {
    }

    // Parameterized constructor for easy command creation
    AICommand(AICommandType type, AICommandPriority prio, uint32_t player = 0, const std::string& data = "") :
        commandType(type),
        priority(prio),
        timestamp(std::chrono::steady_clock::now()),
        commandData(data),
        playerID(player),
        requiresImmediate(prio >= AICommandPriority::PRIORITY_CRITICAL)
    {
    }

    // Comparison operator for priority queue ordering (higher priority first)
    bool operator<(const AICommand& other) const {
        if (priority != other.priority) {
            return priority < other.priority;                           // Higher priority values come first
        }
        return timestamp > other.timestamp;                             // Earlier timestamps come first for same priority
    }
};

//==============================================================================
// Player Behavior Analysis Structures
//==============================================================================
// Player Movement Pattern Analysis Data
struct PlayerMovementPattern {
    Vector2 averageVelocity;                                            // Average movement velocity over time
    Vector2 preferredDirection;                                         // Most commonly used movement direction
    float movementPredictability;                                       // How predictable player movement is (0.0 - 1.0)
    float reactionTime;                                                 // Average reaction time to threats in milliseconds
    float aggressivenessFactor;                                         // How aggressive player movement is (0.0 - 1.0)
    uint32_t totalMovementSamples;                                      // Number of movement samples recorded
    std::chrono::milliseconds sessionDuration;                          // Duration of current analysis session
    std::vector<Vector2> recentPositions;                               // Recent position history for pattern analysis

    // Constructor with default initialization
    PlayerMovementPattern() :
        averageVelocity(Vector2(0.0f, 0.0f)),
        preferredDirection(Vector2(0.0f, 0.0f)),
        movementPredictability(0.5f),
        reactionTime(500.0f),
        aggressivenessFactor(0.3f),
        totalMovementSamples(0),
        sessionDuration(std::chrono::milliseconds(0))
    {
        recentPositions.reserve(1000);                                  // Reserve space for position history
    }
};

// Player Combat Behavior Analysis Data
struct PlayerCombatPattern {
    float accuracyPercentage;                                           // Player shooting accuracy (0.0 - 1.0)
    float preferredEngagementRange;                                     // Preferred distance for combat engagement
    Vector2 preferredCombatPosition;                                    // Preferred position type during combat
    float combatAggression;                                             // Combat aggressiveness factor (0.0 - 1.0)
    uint32_t totalCombatEngagements;                                    // Number of combat encounters analyzed
    uint32_t successfulEngagements;                                     // Number of successful combat encounters
    std::chrono::milliseconds averageCombatDuration;                    // Average length of combat encounters
    uint32_t weaponSwitchFrequency;                                     // How often player switches weapons

    // Constructor with default initialization
    PlayerCombatPattern() :
        accuracyPercentage(0.5f),
        preferredEngagementRange(10.0f),
        preferredCombatPosition(Vector2(0.0f, 0.0f)),
        combatAggression(0.5f),
        totalCombatEngagements(0),
        successfulEngagements(0),
        averageCombatDuration(std::chrono::milliseconds(5000)),
        weaponSwitchFrequency(0)
    {
    }
};

// Player Input Analysis Data
struct PlayerInputPattern {
    uint32_t keyboardActionsPerMinute;                                  // Keyboard actions per minute (APM)
    uint32_t mouseActionsPerMinute;                                     // Mouse actions per minute
    uint32_t joystickActionsPerMinute;                                  // Joystick/gamepad actions per minute
    float inputConsistency;                                             // Consistency of input timing (0.0 - 1.0)
    std::unordered_map<uint32_t, uint32_t> keyboardHeatmap;             // Most frequently used keyboard keys
    std::unordered_map<uint32_t, uint32_t> mouseButtonHeatmap;          // Most frequently used mouse buttons
    Vector2 mouseMovementPattern;                                       // Average mouse movement characteristics
    float inputLatency;                                                 // Average input response latency

    // Constructor with default initialization
    PlayerInputPattern() :
        keyboardActionsPerMinute(0),
        mouseActionsPerMinute(0),
        joystickActionsPerMinute(0),
        inputConsistency(0.5f),
        mouseMovementPattern(Vector2(0.0f, 0.0f)),
        inputLatency(50.0f)
    {
        keyboardHeatmap.reserve(50);                                    // Reserve space for common keys
        mouseButtonHeatmap.reserve(10);                                 // Reserve space for mouse buttons
    }
};

// Comprehensive Player Analysis Result Structure
struct PlayerAnalysisData {
    uint32_t playerID;                                                  // Player identifier
    std::string playerName;                                             // Player name for identification
    uint32_t skillLevel;                                                // Estimated player skill level (1-100)
    float adaptabilityFactor;                                           // How quickly player adapts to AI changes
    PlayerMovementPattern movementData;                                 // Movement pattern analysis
    PlayerCombatPattern combatData;                                     // Combat behavior analysis  
    PlayerInputPattern inputData;                                       // Input pattern analysis
    std::chrono::system_clock::time_point lastAnalysisTime;             // When this data was last updated
    uint32_t sessionsAnalyzed;                                          // Number of game sessions analyzed
    bool isDataValid;                                                   // Whether analysis data is valid and usable

    // Constructor with default initialization
    PlayerAnalysisData() :
        playerID(0),
        playerName("Unknown"),
        skillLevel(50),
        adaptabilityFactor(0.5f),
        lastAnalysisTime(std::chrono::system_clock::now()),
        sessionsAnalyzed(0),
        isDataValid(false)
    {
    }
};

//==============================================================================
// AI Strategic Decision Making Structures
//==============================================================================
// Enemy AI Behavior Strategy Recommendations
struct EnemyAIStrategy {
    float recommendedDifficulty;                                        // Recommended difficulty level (0.0 - 1.0)
    float aggressionLevel;                                              // Recommended AI aggression (0.0 - 1.0)
    float tacticalIntelligence;                                         // AI tactical decision making level (0.0 - 1.0)
    Vector2 recommendedPositioning;                                     // Recommended enemy positioning strategy
    float engagementRange;                                              // Optimal engagement range for enemies
    uint32_t recommendedEnemyCount;                                     // Suggested number of simultaneous enemies
    std::vector<std::string> recommendedTactics;                        // List of recommended tactical approaches
    float predictionAccuracy;                                           // Confidence level in predictions (0.0 - 1.0)
    std::chrono::steady_clock::time_point strategyTimestamp;            // When strategy was generated

    // Constructor with default initialization
    EnemyAIStrategy() :
        recommendedDifficulty(0.5f),
        aggressionLevel(0.5f),
        tacticalIntelligence(0.5f),
        recommendedPositioning(Vector2(0.0f, 0.0f)),
        engagementRange(15.0f),
        recommendedEnemyCount(3),
        predictionAccuracy(0.7f),
        strategyTimestamp(std::chrono::steady_clock::now())
    {
        recommendedTactics.reserve(10);                                 // Reserve space for tactical recommendations
    }
};

// Comprehensive AI Analysis Result for External Use
struct AIAnalysisResult {
    bool isAnalysisValid;                                               // Whether analysis results are valid
    uint32_t analyzedPlayerCount;                                       // Number of players analyzed
    std::vector<PlayerAnalysisData> playerAnalysis;                     // Per-player analysis data
    EnemyAIStrategy recommendedStrategy;                                // Recommended enemy AI strategy
    float overallDifficultyRecommendation;                              // Overall game difficulty recommendation
    std::string analysisNotes;                                          // Additional analysis notes and insights
    std::chrono::steady_clock::time_point analysisTimestamp;            // When analysis was completed
    uint32_t analysisVersion;                                           // Version number for tracking changes

    // Constructor with default initialization
    AIAnalysisResult() :
        isAnalysisValid(false),
        analyzedPlayerCount(0),
        overallDifficultyRecommendation(0.5f),
        analysisNotes(""),
        analysisTimestamp(std::chrono::steady_clock::now()),
        analysisVersion(1)
    {
        playerAnalysis.reserve(8);                                      // Reserve space for maximum players
    }
};

//==============================================================================
// AI Learning Model Configuration
//==============================================================================
struct AIModelConfiguration {
    size_t maxModelSizeBytes;                                           // Maximum AI model size in bytes (default 512MB)
    uint32_t analysisIntervalSeconds;                                   // Analysis interval in seconds (default 30)
    float dataRetentionDays;                                            // How many days to retain old data
    bool enableAdvancedPrediction;                                      // Enable advanced player prediction algorithms
    bool enableCrossSessionLearning;                                    // Enable learning across game sessions
    float learningRate;                                                 // How quickly AI adapts to new data (0.0 - 1.0)
    uint32_t maxPlayerHistoryEntries;                                   // Maximum history entries per player
    bool enableRealTimeAnalysis;                                        // Enable real-time analysis during gameplay

    // Constructor with default configuration values
    AIModelConfiguration() :
        maxModelSizeBytes(512 * 1024 * 1024),                           // 512MB default limit
        analysisIntervalSeconds(30),                                    // 30 second default interval
        dataRetentionDays(30.0f),                                       // 30 days data retention
        enableAdvancedPrediction(true),
        enableCrossSessionLearning(true),
        learningRate(0.1f),
        maxPlayerHistoryEntries(1000),
        enableRealTimeAnalysis(true)
    {
    }
};

//==============================================================================
// Main GamingAI Class Declaration
//==============================================================================
class GamingAI {
public:
    // Constructor and destructor
    GamingAI();
    ~GamingAI();

    //==========================================================================
    // Initialization and Cleanup Methods
    //==========================================================================
    // Initialize the AI system with configuration
    bool Initialize(const AIModelConfiguration& config = AIModelConfiguration());

    // Clean up all AI resources and save current model
    void Cleanup();

    // Check if AI system is properly initialized
    bool IsInitialized() const { return m_isInitialized.load(); }

    //==========================================================================
    // Monitoring Control Methods
    //==========================================================================
    // Start monitoring player behavior for analysis
    bool StartMonitoring();

    // Stop monitoring and finalize current session data
    bool EndMonitoring();

    // Check if currently monitoring player behavior
    bool IsMonitoring() const { return m_isMonitoring.load(); }

    //==========================================================================
    // AI Command Queue Methods
    //==========================================================================
    // Inject a command into the AI processing queue with priority
    bool InjectAICommand(AICommandType commandType, AICommandPriority priority = AICommandPriority::PRIORITY_NORMAL,
        uint32_t playerID = 0, const std::string& commandData = "");

    // Get current command queue size for monitoring
    size_t GetCommandQueueSize() const;

    // Clear all pending commands from queue
    void ClearCommandQueue();

    //==========================================================================
    // Analysis Result Methods
    //==========================================================================
    // Return comprehensive AI analysis results (thread-safe)
    AIAnalysisResult ReturnAIAnalysis();

    // Get current analysis status
    bool IsAnalysisReady() const { return m_analysisReady.load(); }

    // Force immediate analysis update
    bool ForceAnalysisUpdate();

    //==========================================================================
    // Real-time data collection methods (called during monitoring)
    //==========================================================================
    void CollectPlayerPositionData(uint32_t playerID, const Vector2& position);
    void CollectInputEventData(uint32_t inputType, uint32_t inputValue);

    //==========================================================================
    // Configuration Management Methods
    //==========================================================================
    // Update AI configuration settings
    bool UpdateConfiguration(const AIModelConfiguration& config);

    // Get current AI configuration
    AIModelConfiguration GetConfiguration() const;

    // Set maximum AI model size
    void SetMaxModelSize(size_t sizeInBytes);

    // Set analysis interval
    void SetAnalysisInterval(uint32_t seconds);

    //==========================================================================
    // Advanced configuration management methods
    //==========================================================================
    bool UpdateConfigurationParameter(const std::string& parameterName, const std::string& parameterValue);
    std::string ExportConfiguration() const;
    bool ImportConfiguration(const std::string& configString);

    //==========================================================================
    // Model Persistence Methods
    //==========================================================================
    // Save current AI model to disk
    bool SaveAIModel(const std::string& filename = "");

    // Load AI model from disk
    bool LoadAIModel(const std::string& filename = "");

    // Check if AI model file exists
    bool ModelFileExists(const std::string& filename = "") const;

    // Clear all learned AI data and reset to defaults
    void ResetAIModel();

    //==========================================================================
    // Performance Monitoring Methods
    //==========================================================================
    // Get AI system performance statistics
    size_t GetCurrentModelSize() const { return m_currentModelSize.load(); }

    // Get total analysis operations performed
    uint64_t GetTotalAnalysisCount() const { return m_totalAnalysisCount.load(); }

    // Get average analysis processing time
    std::chrono::milliseconds GetAverageAnalysisTime() const;

    // Get AI thread performance metrics
    bool GetThreadPerformanceMetrics(float& cpuUsage, uint64_t& memoryUsage) const;

private:
    //==========================================================================
    // Private Threading Methods
    //==========================================================================
    // Main AI thread processing function
    void AIThreadTasking();

    // Process individual AI commands from queue
    void ProcessAICommand(const AICommand& command);

    // Perform periodic analysis operations
    void PerformPeriodicAnalysis();

    //==========================================================================
    // Private Analysis Methods
    //==========================================================================
    // Analyze player movement patterns for specific player
    void AnalyzePlayerMovement(uint32_t playerID);

    // Analyze player combat behavior patterns
    void AnalyzePlayerCombat(uint32_t playerID);

    // Analyze player input patterns and timing
    void AnalyzePlayerInput(uint32_t playerID);

    // Generate strategic recommendations based on analysis
    void GenerateEnemyStrategy();

    // Update overall difficulty recommendations
    void UpdateDifficultyRecommendations();

    // Clean up outdated analysis data
    void ClearOutdatedData();

    //==========================================================================
    // Private Data Management Methods
    //==========================================================================
    // Load AI model from disk storage
    bool LoadModelFromDisk(const std::string& filename);

    // Save AI model to disk storage
    bool SaveModelToDisk(const std::string& filename);

    // Validate AI model data integrity
    bool ValidateModelData() const;

    // Compress model data for storage efficiency
    bool CompressModelData();

    // Decompress model data after loading
    bool DecompressModelData();

    //==========================================================================
    // Private Utility Methods
    //==========================================================================
    // Get default AI model filename for current platform
    std::string GetDefaultModelFilename() const;

    // Calculate player skill level based on performance metrics
    uint32_t CalculatePlayerSkillLevel(const PlayerAnalysisData& playerData) const;

    // Predict next player action based on historical patterns
    Vector2 PredictPlayerNextAction(uint32_t playerID) const;

    // Calculate adaptive difficulty adjustment
    float CalculateAdaptiveDifficulty(const PlayerAnalysisData& playerData) const;

    // Validate player analysis data quality
    bool ValidateAnalysisData(const PlayerAnalysisData& data) const;

    //==========================================================================
    // Model serialization and deserialization methods
    //==========================================================================
    bool SerializeModelData();
    bool DeserializeModelData();

    //==========================================================================
    // High-Performance Optimized Methods (Assembly when beneficial)
    //==========================================================================
    // Optimized vector distance calculation using SIMD
    float FastVectorDistance(const Vector2& pos1, const Vector2& pos2) const;

    // Optimized pattern matching algorithm
    float FastPatternMatch(const std::vector<Vector2>& pattern1, const std::vector<Vector2>& pattern2) const;

    // Optimized data correlation analysis
    float FastCorrelationAnalysis(const float* data1, const float* data2, size_t dataCount) const;

private:
    //==========================================================================
    // System State Variables
    //==========================================================================
    std::atomic<bool> m_isInitialized;                                  // AI system initialization status
    std::atomic<bool> m_isMonitoring;                                   // Current monitoring status
    std::atomic<bool> m_analysisReady;                                  // Whether analysis results are ready
    std::atomic<bool> m_shouldShutdown;                                 // Signal for thread shutdown
    std::atomic<bool> m_hasCleanedUp;                                   // Whether cleanup has been performed

    //==========================================================================
    // Threading Management
    //==========================================================================
    mutable std::mutex m_commandQueueMutex;                            // Mutex for command queue access
    mutable std::mutex m_analysisDataMutex;                            // Mutex for analysis data access
    mutable std::mutex m_modelDataMutex;                               // Mutex for AI model data access
    std::condition_variable m_commandAvailableCV;                      // Condition variable for command processing

    //==========================================================================
    // AI Command Processing
    //==========================================================================
    std::priority_queue<AICommand> m_commandQueue;                     // Priority queue for AI commands
    std::atomic<size_t> m_commandsProcessed;                           // Total commands processed counter
    std::chrono::steady_clock::time_point m_lastAnalysisTime;          // Last analysis execution time

    //==========================================================================
    // Player Analysis Data Storage
    //==========================================================================
    std::unordered_map<uint32_t, PlayerAnalysisData> m_playerAnalysisData; // Per-player analysis data
    AIAnalysisResult m_currentAnalysisResult;                          // Current analysis results
    EnemyAIStrategy m_currentStrategy;                                  // Current enemy AI strategy

    //==========================================================================
    // AI Model Configuration and Data
    //==========================================================================
    AIModelConfiguration m_configuration;                              // Current AI configuration
    std::atomic<size_t> m_currentModelSize;                            // Current AI model size in bytes
    std::vector<uint8_t> m_aiModelData;                                // Serialized AI model data
    std::string m_modelFilename;                                        // Current model filename

    //==========================================================================
    // Performance Monitoring
    //==========================================================================
    std::atomic<uint64_t> m_totalAnalysisCount;                        // Total analysis operations performed
    std::chrono::steady_clock::time_point m_performanceStartTime;      // Performance monitoring start time
    std::vector<std::chrono::milliseconds> m_analysisTimings;          // Recent analysis timing data

    //==========================================================================
    // Session Monitoring Data
    //==========================================================================
    std::chrono::steady_clock::time_point m_sessionStartTime;          // Current session start time
    std::atomic<uint32_t> m_currentSessionID;                          // Current monitoring session ID
    std::vector<Vector2> m_sessionPlayerPositions;                     // Player positions during current session
    std::vector<uint32_t> m_sessionInputEvents;                        // Input events during current session

    //==========================================================================
    // High-performance optimized methods and assembly implementations
    //==========================================================================
    void FastMemoryCopy(void* dest, const void* src, size_t size) const;
    uint32_t FastDataChecksum(const uint8_t* data, size_t size) const;
    float CalculatePerformanceTrend() const;
    size_t CalculateCurrentModelSize() const;

    //==========================================================================
    // Platform-Specific Performance Optimization
    //==========================================================================
#if defined(PLATFORM_WINDOWS)
    HANDLE m_performanceTimer;                                         // Windows high-resolution timer
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS)
    struct timespec m_performanceTimer;                                // POSIX high-resolution timer  
#endif
};
