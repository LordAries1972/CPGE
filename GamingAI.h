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
#include <array>
#include <ctime>

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
#if defined(PLATFORM_WINDOWS)
    #if !defined(PLATFORM_WINDOWS)
        #define PLATFORM_WINDOWS
    #endif
    #include <windows.h>
#elif defined(PLATFORM_ANDROID)
    #if !defined(PLATFORM_ANDROID)
        #define PLATFORM_ANDROID
    #endif
    #include <unistd.h>
    #include <sys/stat.h>
#elif defined(PLATFORM_LINUX)
    #if !defined(PLATFORM_LINUX)
        #define PLATFORM_LINUX
    #endif
    #include <unistd.h>
    #include <sys/stat.h>
#elif defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)
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
        dataRetentionDays(21.0f),                                       // 21 complete 24-hour periods maximum
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
// CPGE-AI-LISTENER uses typed routes, never executable addresses or arbitrary code.
// Snapshots and replies are value types so game threads never own worker data.
enum class AIGameEnd : uint8_t { Reset, GameOver, Complete, Quit, Abort, SceneChange };
#ifdef None
#pragma push_macro("None")
#undef None
#define CPGE_NONE_SUSPENDED
#endif
// NOTE: X11 defines None as a macro (0L); it is suspended above while
// this enum is declared, then restored below so X11 code (e.g.
// glXMakeCurrent(..., None, ...)) keeps working.
enum class AIGameCommand : uint8_t { None, Pause, Resume, Restart, Aggression };
struct AIGameStrategy {
    float aggression = 0.5f, predictionSeconds = 0.0f, preferredRange = 14.0f;
    float boardCaptureRate = 0.25f, boardCentreRate = 0.5f;
    float movementX = 0.0f, movementY = 0.0f, avoidance = 0.5f;
    uint32_t sessions = 0, recentSessions = 0;
    int recentScore = 0;
    bool ready = false;
};
struct AIDemoPerformance {
    int score = 0, wave = 1, lives = -1;
    float lastProgress = 0.0f, lastLoss = -100.0f;
};
// REPLAY CONTEXT.  Everything a game adapter can observe about HOW a session was being presented, so a recorded or demo session can be shown again the same way: the game mode, the camera
// view mode and angles, the world (weather, time of day, seed) and the music (file name plus time index).  Adapters fill it from their own state; GamingAI journals changes of it with the
// session, keeps the newest per scene/source and hands it back through GetReplayContext.  Restoring it is the adapter's job (it owns the game and audio objects).
struct AIReplayContext {
    bool valid = false;
    int mode = 0, view = 0, weather = 0;
    float yaw = 0, pitch = 0, distance = 0, timeOfDay = 0, musicSeconds = 0;
    uint32_t seed = 0;
    char track[64] = {};                 // music file name, UTF-8, truncated; empty when no music was playing
};
// DEMO STALL WATCHDOG.  Value-only state for GamingAI::ObserveDemoStall.  `strikes` counts the escalations since the last real progress (score / lives / wave change or movement).
struct AIDemoWatchdog {
    float idle = 0, hold = 0, anchorX = 0, anchorY = 0, dirX = 0, dirY = 0;
    int score = -1, lives = -1, wave = -1;
    uint32_t strikes = 0;
    bool anchored = false;
};
#ifdef None
#pragma push_macro("None")
#undef None
#define CPGE_NONE_SUSPENDED
#endif
// NOTE: X11 defines None as a macro (0L); it is suspended above while
// this enum is declared, then restored below so X11 code (e.g.
// glXMakeCurrent(..., None, ...)) keeps working.
enum class AIDemoUnstick : uint8_t { None, Refresh, Probe };
struct AIGameFrame {
    int scene = -1, player = 1, score = 0, lives = 0, wave = 1, mode = 0, floor = -1, area = -1;
    int weapon = -1, ammo = -1, hostiles = -1;
    float height = 0, health = -1, armour = -1, cursorX = 0, cursorY = 0;
    float dt = 0, x = 0, y = 0, moveX = 0, moveY = 0, aimX = 0, aimY = 0;
    uint32_t buttons = 0; uint64_t actions = 0, playback = 0;
    float danger = 0, carryMass = -1, carryCapacity = -1;
    int inventoryCount = -1;
    bool inventoryOpen = false;
    bool demo = false, playing = true, hasPosition = false, over = false, complete = false;
    AIReplayContext replay;              // presentation facts for exact playback (see AIReplayContext); valid only when the adapter filled it
};
struct AIListenerReply {
    uint64_t sequence = 0;
    bool acknowledged = false, direct = false;
    AIGameStrategy strategy;
};
struct AIGameFlow {
    bool hold = false, restart = false, cancel = false;
    int stage = 0;                       // 1: recent activity/startup; 2: game-over restart
    float seconds = 0;
};
struct AINavigationCandidate { float x = 0, y = 0, danger = 0; bool clear = false; };
// Value-only worker messages: no game pointers cross the thread boundary.
struct AIRecoveryPlan {
    uint64_t request = 0, session = 0, revision = 0;
    int scene = -1, player = 1;
    float startX = 0, startY = 0, x = 0, y = 0;
    bool found = false;
};
struct AIRecoveryJob {
    AIRecoveryPlan plan;
    float goalX = 0, goalY = 0;
    int count = 0;
    std::array<AINavigationCandidate, 16> candidates{};
};
// Relative body geometry, independent of rendering visibility or enemy role.
// Adapters provide legal escape endpoints in their game's movement space.
struct AIDemoContact { float x = 0, y = 0, z = 0, radius = 1; };
struct AIDemoEscapeCandidate { float x = 0, y = 0, z = 0; bool clear = false; };
struct AIDemoEscapeMemory {
    float hold = 0, x = 0, y = 0, z = 0;
    bool active = false;
};
// Game adapters supply observable collision/attack facts; avoidance policy stays in GamingAI.
struct AICombatThreat {
    float x = 0, y = 0, vx = 0, vy = 0, radius = 0;
    float impactTime = -1.0f;               // negative: moving threat; otherwise seconds until a blast
    bool boss = false;
    bool crushing = false;
};
struct AINavigationMemory {
    std::array<std::array<float, 2>, 32> visited{};
    int count = 0, next = 0, failures = 0;
    float lastX = 0, lastY = 0, stalled = 0, sampleTime = 0;
};
#ifdef None
#pragma push_macro("None")
#undef None
#define CPGE_NONE_SUSPENDED
#endif
// NOTE: X11 defines None as a macro (0L); it is suspended above while
// this enum is declared, then restored below so X11 code (e.g.
// glXMakeCurrent(..., None, ...)) keeps working.
enum class AIInventoryOperation : uint8_t { None, Open, Use, Equip, Drop, Close };
enum class AIInventoryKind : uint8_t { Equipment, Recovery, Resource, Weapon, Ammo, Key };
struct AIInventoryItem {
    int id = -1, count = 0, quality = 0, equippedQuality = 0;
    float weight = 0, need = 0;
    AIInventoryKind kind = AIInventoryKind::Resource;
    bool usable = false, essential = false;
};
struct AIInventoryState {
    float mass = 0, capacity = 0, requiredSpace = 0;
    bool full = false, open = false, safe = false, hasWindow = true, autonomous = false;
};
struct AIInventoryMemory { float wait = 0; bool prompted = false; };
struct AIInventoryDecision { AIInventoryOperation operation = AIInventoryOperation::None; int item = -1, amount = 1; };
struct AIGroundVehicleMemory {
    float replan = 0, targetX = 0, targetY = 0, speed = 0;
};
struct AITerrainCell { float height = 0; bool clear = false, canyon = false, bridge = false; };
enum class AIDemoGoalKind : uint8_t { Enemy, Supply, Heal, Collect, Explore };
struct AIDemoGoal {
    uint64_t id = 0;
    float x = 0, y = 0;
    AIDemoGoalKind kind = AIDemoGoalKind::Explore;
    bool visible = false, sameArea = true, available = true;
};
struct AIDemoPlan {
    uint64_t goal = 0;
    std::array<uint64_t, 8> rejected{};
    std::array<float, 8> retryAt{};
    int nextRejected = 0;
    float time = 0, progressTime = 0, bestDistance = 1.0e30f;
    float lastHealth = 1, damagePressure = 0;
    uint32_t decisions = 0, recoveries = 0;
};
using AITerrainReader = AITerrainCell (*)(const void*, int);
struct AIGroundVehicleState {
    float x = 0, y = 0, targetX = 0, targetY = 0;
    float baseSpeed = 0, range = 0, radius = 0, shotCooldown = 0;
    int level = 1;
    bool tracked = false, artillery = false, visible = false;
};

enum class AIBoardGoal : uint8_t { Develop, KingSafety, Initiative, Endgame };
struct AIBoardPlan {
    uint64_t request = 0, position = 0;
    int scene = 33, iq = 100, moveNumber = 1, purpose = 0;
    AIBoardGoal goal = AIBoardGoal::Develop;
    bool acknowledged = false, demo = false;
};
struct AIBoardCandidate {
    int from = -1, to = -1, promotion = 0, piece = 0, captured = 0;
    uint32_t flags = 0;                 // 1 capture, 2 check, 4 promotion, 8 castle
    float centre = 0;
    bool development = false;
};
struct AIBoardStatus {
    AIBoardPlan plan;
    AIBoardCandidate move;
    uint64_t resultingPosition = 0, observedPosition = 0;
    int score = 0, depth = 0;
    bool ready = false, accepted = false, human = false, applied = false;
};

// ================================================================================================
//  SPACE DOGFIGHT POLICY (System Explorer).  Value-only: the game adapter measures the target in the pilot's LOCAL frame (x right, y up, z forward) and executes the returned stick orders.
//  The state machine is modelled on the pilot blocks of Freelancer's MISSIONS/pilots_population.ini:
//    BUZZ (BuzzHeadToward: close at 0.8 throttle sliding left / right) -> PASSBY (BuzzPassBy: break away 90 deg) -> TRAIL (get on the tail, lock cone 30 deg) -> STRAFE (attack run, then run away)
//    EVADE_DODGE (slide / corkscrew when targeted), EVADE_BREAK (hard break after heavy damage), ENGINE_KILL (face the target to disable its engine), plus guns in bursts with an accuracy cone,
//    missiles (interval / range / cone) and mines when the target is on the pilot's tail.  Skill tiers 0 easy .. 3 ace use the INI's accuracy cones (15 -> 6 deg) and burst gaps.
// ================================================================================================
enum class AIDogState : uint8_t { Buzz = 0, PassBy, Trail, Strafe, Dodge, Break, EngineKill, Flee };
struct AIDogfightMemory {
    AIDogState state = AIDogState::Buzz;
    float stateT = 0, dodgeT = 0, dodgeSide = 1, dodgeUp = 0, burstT = 0, missileT = 6, mineT = 4, lastHp = -1, wobble = 0, rollDir = 1;
    bool burstOn = true; uint32_t rng = 2463534242u;
};
struct AIDogfightFacts {
    float lx = 0, ly = 0, lz = 1;       // direction to the target's lead point in the pilot's local frame (not necessarily unit length)
    float dist = 1000, closing = 0;     // metres to the target, closing speed (positive = approaching)
    float tgtFacingMe = 0;              // cosine of the angle between the target's nose and the line to the pilot (1 = the target is aiming at the pilot)
    float hpFrac = 1, gunRange = 700, dt = 0.016f;
    int skill = 1;                      // 0 easy, 1 medium, 2 hard, 3 ace
    bool hasMissile = false, hasMine = false, tgtEngineDead = false, missileInbound = false;
};
struct AIDogfightOrders {
    float yaw = 0, pitch = 0, roll = 0, throttle = 1, slideX = 0, slideY = 0;      // -1..1 stick values; slide = lateral / vertical thruster
    bool fire = false, missile = false, mine = false, boost = false, countermeasure = false, disableEngine = false;
};
// ================================================================================================
//  SPACE LANDING POLICY (System Explorer): how any ship (player autopilot, attract-mode pilot, traders) lands on a planet through its ENTRY PORTAL or docks at a station bay.
//  Value-only like the dogfight policy: the game measures the approach point, the landing point and the pilot's offset from the landing axis, and executes the returned stick orders.
//    APPROACH (fly to the standoff point out on the landing axis; cruise drive when far, with a detour flag when a body is in the way) ->
//    ALIGN    (creep onto the axis, nose toward the portal) -> DESCEND (a funnel: speed falls with distance, drifting out of the funnel sends the ship back to ALIGN) -> land.
// ================================================================================================
// OBSTACLE AVOIDANCE (System Explorer autopilot, attract-mode pilot, traders): spheres in the pilot's local frame (planets, stations, the star, a black hole) and the direction to the goal in, the
// direction to AIM at out.  When a sphere (grown by a clearance that rises with speed, because a fast ship turns wide) lies across the path, the aim point becomes a waypoint beside it, so the ship
// skirts the body and the path is flown safely; close in and fast it also asks for the cruise drive to be dropped and the throttle to be eased so the turn can be made.
struct AIObstacle { float x = 0, y = 0, z = 0, radius = 0; };
struct AIAvoidOrders { float ax = 0, ay = 0, az = 1; bool avoiding = false, dropCruise = false; float throttleScale = 1.0f; };
// MARKET POLICY (System Explorer): how a system's prices follow its supply and demand.  The game measures the system's baseline (what its faction specialises in: plentiful or in demand),
// the player's own trading (delta) and a slow wobble, and takes back the price: plentiful goods are cheap (down to x0.42), goods in demand are dear (up to x2.35), so carrying supplies INTO a
// system that lacks them pays.  Buying drains supply, selling feeds it, and the market drifts back to its baseline with time.
struct AIMarketFacts { int basePrice = 100; float baseline = 0, delta = 0, wobble = 0; };       // baseline / delta / wobble: supply contributions, +1 = plenty, -1 = scarce
struct AIMarketQuote { float supply = 0, mult = 1; int price = 100; };
enum class AILandPhase : uint8_t { Approach = 0, Align, Descend };
struct AILandingMemory {
    AILandPhase phase = AILandPhase::Approach;
    float phaseT = 0, totalT = 0;                                      // seconds in this phase / since the landing was started (a caller may abandon a landing that drags on)
};
struct AILandingFacts {
    float ax = 0, ay = 0, az = 1, apDist = 1000;                       // the approach point in the pilot's local frame (x right, y up, z forward) and its distance
    float lx = 0, ly = 0, lz = 1, dist = 1000;                         // the landing point (the portal's centre / the bay) likewise
    float along = 0, lateral = 0;                                      // the pilot's offset from the landing point: metres OUT along the landing axis (negative = on the wrong side) and metres off the axis
    float speed = 0, spdMax = 300, maxLandSpeed = 120, radius = 150, standoff = 1200, dt = 0.016f;
    bool cruising = false, cruiseCharging = false, detour = false;     // detour: the approach point given is a waypoint round a planet, not the real standoff point
};
struct AILandingOrders {
    float yaw = 0, pitch = 0, throttle = 0;                            // stick values, throttle as a fraction of the ship's top speed
    bool cruiseToggle = false;                                         // the pilot should press the cruise key (on when far and lined up, off when near)
    bool requestLand = false;                                          // inside the portal / bay at a safe speed: land now
    AILandPhase phase = AILandPhase::Approach;
};

// ================================================================================================
//  MASTER AI SPACE COMMAND (System Explorer demo / attract mode).  Value-only: the game adapter reports what it can observe (hostile contacts, candidate destinations, the pilot's condition, the jobs it
//  carries) and GamingAI::PlanSpaceMission answers with WHAT TO DO.  It does four things the old "nearest hostile, else a random point" pilot did not:
//    THREAT ASSESSMENT   every contact gets a danger score (role weight x skill x proximity x "is aiming at me"); the sum against the pilot's own strength (hull, shields, ammunition, energy) is the
//                        fight ratio that decides between engaging, evading, falling back and ignoring distant contacts.
//    TASK PRIORITIES     survive > repair > fight what is attacking > the JOB (work in this system, or the trip to the job's system) > explore.  A destination is therefore always a job's, and only
//                        without a job an exploration target or a task (picking up work at a station, discovering a planet, scanning an anomaly).
//    PROBLEM SOLVING     progress toward the current goal is watched; no progress for a while rejects the goal for a time and the next best is taken (a wedged approach, an unreachable pick).
//    FLIGHT PATHING      the answer names the goal; the adapter flies it with obstacle avoidance, the landing policy, cruise-drive rules and a lead-pursuit intercept for moving targets.
// ================================================================================================
enum class AISpaceMode : uint8_t { Patrol = 0, Travel, Job, Engage, Evade, Retreat, Repair };
enum class AISpaceGoalKind : uint8_t { Gate = 0, Station, Planet, Anomaly, Derelict, Hostile, Wander };
enum class AISpaceReason : uint8_t { Idle = 0, JobWork, JobTravel, SeekWork, Explore, Hostiles, Missile, Outgunned, Damaged, Stalled };
struct AISpaceContact { int id = -1; float dist = 9999, closing = 0, hpFrac = 1, weight = 1, skill = 1; bool facingMe = false; };       // one live hostile (weight: ordinary raider 1, an ace 1.5, a capital 3, a boss 6)
struct AISpaceGoal {
    int id = -1; AISpaceGoalKind kind = AISpaceGoalKind::Wander; float dist = 0;
    float jobValue = 0;          // 0..1: how much an ACCEPTED JOB needs this goal (the hostiles of a kill job, the derelict of a salvage job, the gate toward a job's system ...)
    float value = 0;             // 0..1: intrinsic interest when there is no job (unvisited system, undiscovered planet, work waiting at a station, an anomaly worth scanning)
    float hazard = 0;            // 0..1: how dangerous reaching it is (mine field, ion storm, pulsar beam)
    bool onRoute = false;        // a gate that leads toward the job's system
};
struct AISpaceFacts {
    float hull = 1, shield = 1, energy = 1, dt = 0.016f, havenDist = 1.0e9f;      // fractions 0..1; metres to the nearest station / portal (a haven where the police and a free refit wait)
    int missiles = 0; bool haveHaven = false, inSafeZone = false, missileInbound = false, hasJob = false, jobWorkHere = false, warping = false;       // warping: on an autopilot trip with the warp drive running - only a contact that is close or attacking breaks it
};
struct AISpaceMemory {
    AISpaceMode mode = AISpaceMode::Patrol; AISpaceReason reason = AISpaceReason::Idle;
    float modeT = 0, clock = 0, goalT = 0, bestDist = 1.0e30f, stallT = 0, lastVit = -1, pressure = 0, threatSm = 0, evadeT = 0, attackedT = 0;      // attackedT: seconds left of "someone attacked us first" - the pilot keeps fighting back for that long
    int goalId = -1; AISpaceGoalKind goalKind = AISpaceGoalKind::Wander; int targetId = -1; bool cruiseOk = true;                    // warp permitted (hysteresis around the nearest hostile)
    std::array<int, 6> rejId{}; std::array<uint8_t, 6> rejKind{}; std::array<float, 6> rejUntil{}; int nextRej = 0;
    uint32_t decisions = 0, stalls = 0;
};
struct AISpaceOrders {
    AISpaceMode mode = AISpaceMode::Patrol; AISpaceReason reason = AISpaceReason::Idle;
    int targetContact = -1;                    // index into the contact array to fight / evade (Engage, Evade)
    int goalIndex = -1;                        // index into the goal array to fly to (every mode but Engage / Evade); haven modes name the nearest station / portal goal
    bool goalChanged = false, allowCruise = false;
    float threat = 0, strength = 1, fightRatio = 0;
};


// ================================================================================================
//  STEADY AIM and WARP POLICY (any pilot that flies to a goal in 3D: a player's autopilot, a demo pilot, an NPC trader).  World space, value-only.
// ================================================================================================
// STEADY AIM: a ship's velocity chases its nose with a lag, so aiming the nose straight at the goal makes it DRIFT sideways at speed, and an obstacle-avoidance waypoint can flip from one side of a body to the
// other between frames.  SteadyAim subtracts the sideways part of the velocity from the aim (velocity compensation: the VELOCITY, not the nose, closes on the goal) and low-passes the result (about 0.14 s);
// a gap of more than 0.25 s between calls (a new trip) restarts the smoothing.  Keep one AISteadyAim per pilot.
struct AISteadyAim { float wx = 0, wy = 0, wz = 1, lastTime = -1.0e9f; };
struct AISteadyAimFacts
{
    float gx = 0, gy = 0, gz = 1;           // direction to the goal or to the avoidance waypoint (any length), world space
    float vx = 0, vy = 0, vz = 0;           // the pilot's velocity, world space
    float dist = 0, arrive = 100;           // metres to the goal; inside 2 x arrive no compensation is applied (the final approach is flown on the nose)
    float time = 0, dt = 0.016f;            // game clock and the frame's dt
};
// WARP POLICY: one steady rule for a drive that takes seconds to spin up.  A trip longer than engageDist uses the drive once the pilot is roughly lined up (steerDemand < 0.25); the drive is dropped only
// on arrival (inside dropDist), when it is no longer allowed (a fight), or when an obstacle is really close - and an obstacle drop locks the drive out for lockSeconds so it cannot flicker on and off.
struct AIWarpMemory { float lockUntil = 0; };
struct AIWarpFacts
{
    float time = 0, dist = 0, steerDemand = 0;          // game clock, metres to the goal, how hard the pilot is still turning (0..1, the |yaw| order)
    float engageDist = 4500.0f, dropDist = 3000.0f, lockSeconds = 10.0f;
    bool cruising = false, charging = false, allowed = true, obstacleClose = false;
};

class GamingAI {
public:
    // Constructor and destructor
    GamingAI();
    ~GamingAI();

    //==========================================================================
    // Initialization and Cleanup Methods
    //==========================================================================
    // Initialize the AI system with configuration
    // Shared vector-game lifecycle. Only the game thread calls frame/lifecycle methods.
    // Disk IO, pruning and rolling-session analysis run on the existing AI worker.
    void ConfigureGameLearning(bool record, bool recall, bool adapt, bool restart);
    uint64_t CurrentGameSession() const;
    void RecordPlayback(uint64_t session, uint64_t playback, const AIGameFrame& result, float seconds);
    static AIInventoryDecision PlanInventory(AIInventoryMemory& memory, const AIInventoryState& state, const AIInventoryItem* items, int count, float dt);
    uint64_t RecordInventoryDirective(int scene, AIInventoryOperation operation, int item, bool automatic);
    void RecordInventoryResult(uint64_t sequence, bool applied, float mass, float health);
    AIGameFlow AdvanceGameLifecycle(int scene, bool demo, bool reset, float dt,
        bool confirm, bool cancel, bool allowCountdown, int player = 1);
    void RecordGameUpdate(const AIGameFrame& frame);
    void NotifyGameReset();
    void EndGameSession(AIGameEnd reason);
    AIListenerReply RequestGameStrategy(int scene, int player = 1, bool demo = false);
    uint64_t RequestGameRecovery(int scene, uint64_t revision, float x, float y,
        float goalX, float goalY, const AINavigationCandidate* candidates, int count);
    bool PollGameRecovery(int scene, uint64_t revision, AIRecoveryPlan& plan);
    bool SubmitExternalGameCommand(int scene, AIGameCommand command, float value,
        uint64_t sequence, int player = 1);
    bool PollGameCommand(int scene, AIGameCommand& command, float& value, uint64_t& sequence);
    void RecordGameCommandResult(uint64_t sequence, bool applied);
    bool GameHistoryHealthy() const;
    void ResetGameEncounters();
    bool EncounterDefeated(uint64_t area, int kind) const;
    void RecordEncounterDefeat(uint64_t area, int kind);
    static bool FullBoardStrength(int iq);
    AIBoardPlan RequestBoardStrategy(int scene, uint64_t position, int iq, int moveNumber, int pieces, int purpose, bool demo);
    static void OrderBoardMoves(const AIBoardPlan& plan, const AIBoardCandidate* moves, int count, int* order);
    bool AcknowledgeBoardSuggestion(const AIBoardPlan& plan, const AIBoardCandidate& move, int score, int depth, bool valid);
    void RecordBoardMove(int scene, uint64_t before, uint64_t after, uint64_t request, const AIBoardCandidate& move, bool human, bool demo, bool applied = true);
    AIBoardStatus GetBoardStatus(int scene) const;
    void ResetBoardAdvisor(int scene);
    static float InitialBossStrength(int wave);
    static bool NavigationStepAllowed(float fromX, float fromY, float toX, float toY, float width, float height, float margin = 4.0f);
    static uint64_t SelectThreatBudget(int count, float pressure, uint32_t seed);
    static int PlanNavigationStep(AINavigationMemory& memory, float x, float y,
        float goalX, float goalY, float dt, const AINavigationCandidate* candidates,
        int count, const AIGameStrategy& strategy, bool safetyFirst = false, bool exploreIfIdle = false);
    static bool ValidateGameRecording(const std::filesystem::path& path, int expectedScene = -1);
    static float CombatMoveDanger(const AICombatThreat* threats, int count,
        float x, float y, float vx, float vy, float horizon);
    static float BossCombatRange(float weaponRange, float bodyRadius, float healthFraction);
    static bool ShouldUseCombatMedkit(float healthFraction, int medkits);
    // Demo stall policy.  The adapter calls ObserveDemoStall once per demo frame with facts it already has; the core decides when the demo is stuck and what to do:
    //   Refresh - drop cached advice and per-game bot memory (about 4 s without progress), Probe - hold the returned unit direction for a short pulse of input (every 6 s after that).
    // Progress is a score / lives / wave change or the position moving more than a short distance; games without a position are never flagged (board games decide their own moves).
    static AIDemoUnstick ObserveDemoStall(AIDemoWatchdog& state, float dt, bool playing, bool hasPosition, float x, float y,
        int score, int lives, int wave, float& dirX, float& dirY);
    // Longest a board-game search may run before the adapter plays a legal fallback: the configured limit plus slack, and a much shorter bound for the attract demo.
    static float BoardSearchDeadline(int timeLimitSeconds, bool demo, float unlimitedSeconds);
    // Newest presentation context journalled for a scene and source (this run's live copy first, then the persisted file loaded by the worker).  False when nothing was recorded.
    bool GetReplayContext(int scene, bool demo, AIReplayContext& out) const;
    static float DemoPlanningInterval(const AIGameStrategy& strategy, bool heavyThreat);
    static float DemoPredictionHorizon(const AIGameStrategy& strategy);
    static float GroundVehicleSpeed(float baseSpeed, int level);
    static bool AllowCanyonStep(bool canyon, bool bridge, bool fallback);
    static int FindTerrainRoute(int width, int height, int start, int goal, uint64_t revision,
        AITerrainReader reader, const void* context, int* path, int capacity, bool& reached, bool& usedCanyon, float border = 4.0f);
    // Plan -> execute in the adapter -> observe next call. Fixed memory and bounded
    // candidate counts keep decisions independent of disk IO and worker latency.
    static int PlanDemoGoal(AIDemoPlan& plan, const AIDemoGoal* goals, int count,
        float x, float y, float health, bool needsAmmo, float dt, const AIGameStrategy& strategy);
    static void RejectDemoGoal(AIDemoPlan& plan);
    static bool CanFireFacing(float angularError);
    static int DemoViewMode(float elapsed, const AIGameStrategy& strategy);
    static void ObserveDemoPerformance(AIDemoPerformance& state, int score, int wave, int lives, float elapsed);
    static float DemoTimeLimit(const AIDemoPerformance& state, float elapsed, float normalLimit);
    static int PlanDemoSeparation(AIDemoEscapeMemory& memory, const AIDemoContact* contacts,
        int count, const AIDemoEscapeCandidate* candidates, int candidateCount, float dt);
    static float GunnerPositionScore(float targetDistance, int coverFaces, int firingLanes);
    static bool GunnerShouldAdvance(bool visible, float distance, float weaponRange, float contactAge);
    // One step of the space dogfight state machine (see AIDogfightMemory).  Also used by the attract-mode autopilot of System Explorer.
    static AIDogfightOrders StepDogfighter(AIDogfightMemory& memory, const AIDogfightFacts& facts);
    // Steering toward a local-frame point without shooting: yaw / pitch proportional to the bearing, throttle eased down on arrival (a waypoint flight).
    static AIDogfightOrders FlyToward(float lx, float ly, float lz, float dist, float arriveDist, float cruiseThrottle);
    // One step of the landing approach (see AILandingMemory): planets through their entry portals, stations through their bays.
    static AILandingOrders StepLanding(AILandingMemory& memory, const AILandingFacts& facts);
    // Master AI space command (see AISpaceMemory): threat assessment, task priorities, stall solving.  Called a few times a second by the System Explorer demo adapter; contacts / goals are caller arrays.
    static AISpaceOrders PlanSpaceMission(AISpaceMemory& memory, const AISpaceFacts& facts, const AISpaceContact* contacts, int contactCount, const AISpaceGoal* goals, int goalCount);
    static void ResetSpaceMemory(AISpaceMemory& memory);                                    // a new system or a new game: forget goals and rejections (decision counters survive)
    // Steer round the spheres that lie across the path to the goal (see AIObstacle).
    // Market: a quote from the facts, the supply change of a trade of `units`, and the drift back toward the baseline over `seconds`.
    // Reputation and law (System Explorer): the price factor a faction's standing earns (+100 = 15 % off, -100 = 15 % extra), whether its police attack the player on sight (they do when the player is wanted
    // or the standing is below -40) and the bounty a private mission pays (base per unit x units, more for distance and threat, a little random).
    static float ReputationPriceMul(int reputation);
    static bool  PoliceHostileToPlayer(int reputation, float wanted);
    static int   MissionBounty(float basePerUnit, int units, int hops, float threat, float roll);
    static AIMarketQuote QuoteMarket(const AIMarketFacts& facts);
    static float MarketTrade(float delta, int units, bool bought);
    static float MarketRecover(float delta, float seconds);
    static AIAvoidOrders AvoidObstacles(const AIObstacle* obstacles, int count, float goalX, float goalY, float goalZ, float speed);
    // Steady aim and the warp rule (see AISteadyAim / AIWarpMemory).  SteadyAim writes the unit aim direction (world space); StepWarp returns true when the pilot should press the cruise key this frame.
    static void SteadyAim(AISteadyAim& state, const AISteadyAimFacts& facts, float& outX, float& outY, float& outZ);
    static bool StepWarp(AIWarpMemory& memory, const AIWarpFacts& facts);
    static void BuildReachability(int width, int height, int start, AITerrainReader reader,
        const void* context, uint8_t* reachable, int capacity);
    static void PlanGroundVehicle(AIGroundVehicleMemory& memory, const AIGroundVehicleState& state,
        const AINavigationCandidate* candidates, int count, const AICombatThreat* threats, int threatCount);

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
    class GameHistory;
    std::unique_ptr<GameHistory> m_gameHistory;
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
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_MACOS) || defined(PLATFORM_ANDROID) || defined(PLATFORM_IOS)
    struct timespec m_performanceTimer;                                // POSIX high-resolution timer  
#endif
};

// X11's None macro was suspended above (before the enum declarations
// that use None as an enumerator). Restore it here, at the end of this
// header, so X11 code after the includes (e.g. glXMakeCurrent(...,
// None, ...)) keeps working. The restore must be at end-of-header,
// not right after each enum: enumerator uses later in this header
// (default member initialisers such as TunnelSpinCycle::None) are
// parsed while the macro is suspended.
#ifdef CPGE_NONE_SUSPENDED
#pragma pop_macro("None")
#undef CPGE_NONE_SUSPENDED
#endif
