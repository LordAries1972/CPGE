// -------------------------------------------------------------------------------------------------------------
// GamePlayer.h - Comprehensive Game Player Management System
// 
// This class provides a complete player management system supporting up to 8 players with comprehensive
// game statistics, network integration, collision detection, and multi-platform compatibility.
// Designed to work with various game types and renderer backends while maintaining platform independence.
// -------------------------------------------------------------------------------------------------------------
#pragma once

#define _USE_NETWORKMANAGER_                                            // Comment this line if you are not USING Networking features.

#include "Includes.h"
#include "Debug.h"
#include "Vectors.h"
#include "Color.h"
#include "Renderer.h"

#if defined(_USE_NETWORKMANAGER_)
    #include "NetworkManager.h"

    extern NetworkManager networkManager;
#endif

#include "PUNPack.h"

#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <atomic>

// Forward declarations
extern Debug debug;
extern PUNPack punPack;

//==============================================================================
// Game Type Enumeration - Defines various game genres and styles
// These can be combined using bitwise OR operations for hybrid games
//==============================================================================
enum class GameType : uint32_t {
    GT_NONE = 0x00000000,                                               // No game type specified
    GT_SHOOTEMUP = 0x00000001,                                          // Classic shoot-em-up games
    GT_RPG = 0x00000002,                                                // Role-playing games
    GT_FANTASY = 0x00000004,                                            // Fantasy themed games
    GT_PLATFORM = 0x00000008,                                           // Platform jumping games
    GT_SPACE = 0x00000010,                                              // Space themed games
    GT_ACTION = 0x00000020,                                             // Action oriented games
    GT_TOPDOWN = 0x00000040,                                            // Top-down view games
    GT_ARCADE = 0x00000080,                                             // Arcade style games
    GT_VECTOR = 0x00000100,                                             // Vector graphics games
    GT_3D = 0x00000200,                                                 // 3D perspective games
    GT_FPS = 0x00000400,                                                // First-person shooter games
    GT_GOTCHA = 0x00000800                                              // Gotcha/surprise mechanic games
};

// Bitwise operators for GameType combinations
inline GameType operator|(GameType a, GameType b) {
    return static_cast<GameType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline GameType operator&(GameType a, GameType b) {
    return static_cast<GameType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline GameType& operator|=(GameType& a, GameType b) {
    return a = a | b;
}

//==============================================================================
// Player Activity States - Defines current player status
//==============================================================================
enum class PlayerState : uint8_t {
    INACTIVE = 0,                                                       // Player not participating
    ACTIVE = 1,                                                         // Player actively playing
    DEAD = 2,                                                           // Player is dead but may respawn
    RESPAWNING = 3,                                                     // Player is in respawn process
    SPECTATING = 4,                                                     // Player watching others
    PAUSED = 5,                                                         // Player game is paused
    DISCONNECTED = 6                                                    // Network player disconnected
};

//==============================================================================
// Animation States for Player Death Effects
//==============================================================================
enum class DeathAnimationState : uint8_t {
    NONE = 0,                                                           // No death animation active
    EXPLOSION = 1,                                                      // Explosion animation playing
    FADE_OUT = 2,                                                       // Fade out effect active
    DISINTEGRATION = 3,                                                 // Disintegration effect
    CUSTOM_EFFECT = 4                                                   // Custom death effect
};

//==============================================================================
// Comprehensive Player Information Structure
// Contains all data necessary for player management across different game types
//==============================================================================
struct PlayerInfo {
    // Basic Player Identification
    int playerID;                                                       // Unique player identifier (0-7)
    std::string playerName;                                             // Player display name
    std::string playerTag;                                              // Player clan/team tag
    MyColor playerColor;                                                // Player theme color

    // Visual Representation
    BlitObj2DIndexType portraitImageIndex;                              // 2D portrait image reference
    BlitObj2DIndexType frameImageIndex;                                 // 2D frame image reference

    // Position and Movement Data
    Vector2 position2D;                                                 // Current 2D game position
    Vector3 position3D;                                                 // Current 3D game position
    Vector2 velocity2D;                                                 // 2D movement velocity
    Vector3 velocity3D;                                                 // 3D movement velocity
    Vector2 mapPosition;                                                // Position on game map
    float rotation;                                                     // Player rotation angle in degrees

    // Player States and Status
    PlayerState currentState;                                           // Current player activity state
    bool isDead;                                                        // Player death status flag
    bool isActive;                                                      // Player participation flag
    DeathAnimationState deathAnimation;                                 // Current death animation state

    // Health and Combat Statistics
    int health;                                                         // Current health points
    int maxHealth;                                                      // Maximum health capacity
    int armour;                                                         // Current armour protection
    int maxArmour;                                                      // Maximum armour capacity
    int shield;                                                         // Energy shield strength
    int maxShield;                                                      // Maximum shield capacity

    // Game Scoring and Progress
    uint64_t score;                                                     // Player current score
    uint64_t highScore;                                                 // Player best score
    int lives;                                                          // Remaining lives count
    int level;                                                          // Current player level
    uint64_t experience;                                                // Experience points earned
    uint64_t experienceToNext;                                          // Experience needed for next level

    // RPG and Fantasy Game Statistics
    int strength;                                                       // Physical power attribute
    int intelligence;                                                   // Mental power attribute
    int dexterity;                                                      // Agility and precision attribute
    int constitution;                                                   // Health and endurance attribute
    int charisma;                                                       // Social interaction attribute
    int wisdom;                                                         // Perception and intuition attribute
    int luck;                                                           // Random event modifier

    // Combat and Weapon Systems
    int attackPower;                                                    // Base attack damage
    int defenseRating;                                                  // Damage reduction rating
    int criticalChance;                                                 // Critical hit probability (0-100)
    int criticalMultiplier;                                             // Critical damage multiplier
    float attackSpeed;                                                  // Attacks per second rate
    float movementSpeed;                                                // Movement velocity modifier

    // Resource Management
    int mana;                                                           // Magical energy points
    int maxMana;                                                        // Maximum mana capacity
    int energy;                                                         // Special ability energy
    int maxEnergy;                                                      // Maximum energy capacity
    int ammunition;                                                     // Current ammunition count
    int maxAmmunition;                                                  // Maximum ammunition capacity

    // Timing and Event Management
    bool timerActive;                                                   // Timer system activation flag
    std::chrono::steady_clock::time_point timerStart;                   // Timer start timestamp
    std::chrono::steady_clock::time_point timerCurrent;                 // Current timer timestamp
    std::chrono::milliseconds totalTimeElapsed;                         // Total elapsed time duration

    // Collision Detection System
    std::vector<uint8_t> collisionBitmap;                               // Bitmap for collision detection
    int bitmapWidth;                                                    // Collision bitmap width
    int bitmapHeight;                                                   // Collision bitmap height
    Vector2 collisionOffset;                                            // Collision detection offset

    // Inventory and Equipment (for RPG games)
    std::vector<int> inventory;                                         // Player inventory item IDs
    int equippedWeapon;                                                 // Currently equipped weapon ID
    int equippedArmour;                                                 // Currently equipped armour ID
    int equippedAccessory;                                              // Currently equipped accessory ID

    // Achievement and Progress Tracking
    std::vector<int> unlockedAchievements;                              // List of earned achievement IDs
    std::vector<int> completedQuests;                                   // List of completed quest IDs
    std::vector<int> discoveredAreas;                                   // List of explored map area IDs

    #if defined(_USE_NETWORKMANAGER_)
        // Network and Multiplayer Data
        bool isNetworkPlayer;                                               // Network player identification
        std::string networkSessionID;                                       // Network session identifier
        uint32_t networkLatency;                                            // Network ping latency in ms
    #endif

    // Constructor with default initialization
    PlayerInfo() :
        playerID(-1),                                                   // Invalid player ID by default
        playerName("Unknown"),                                          // Default player name
        playerTag(""),                                                  // Empty clan tag
        playerColor(MyColor::White()),                                  // Default white color
        portraitImageIndex(BlitObj2DIndexType::NONE),                   // No portrait image
        frameImageIndex(BlitObj2DIndexType::NONE),                      // No frame image
        position2D(Vector2(0.0f, 0.0f)),                                // Origin position 2D
        position3D(Vector3(0.0f, 0.0f, 0.0f)),                          // Origin position 3D
        velocity2D(Vector2(0.0f, 0.0f)),                                // No initial velocity 2D
        velocity3D(Vector3(0.0f, 0.0f, 0.0f)),                          // No initial velocity 3D
        mapPosition(Vector2(0.0f, 0.0f)),                               // Origin map position
        rotation(0.0f),                                                 // No rotation
        currentState(PlayerState::INACTIVE),                            // Inactive by default
        isDead(false),                                                  // Alive by default
        isActive(false),                                                // Not active by default
        deathAnimation(DeathAnimationState::NONE),                      // No death animation
        health(100),                                                    // Default health value
        maxHealth(100),                                                 // Default max health
        armour(0),                                                      // No initial armour
        maxArmour(100),                                                 // Default max armour
        shield(0),                                                      // No initial shield
        maxShield(100),                                                 // Default max shield
        score(0),                                                       // No initial score
        highScore(0),                                                   // No high score
        lives(3),                                                       // Standard 3 lives
        level(1),                                                       // Start at level 1
        experience(0),                                                  // No initial experience
        experienceToNext(1000),                                         // 1000 XP to level 2
        strength(10),                                                   // Base strength
        intelligence(10),                                               // Base intelligence
        dexterity(10),                                                  // Base dexterity
        constitution(10),                                               // Base constitution
        charisma(10),                                                   // Base charisma
        wisdom(10),                                                     // Base wisdom
        luck(10),                                                       // Base luck
        attackPower(10),                                                // Base attack power
        defenseRating(5),                                               // Base defense
        criticalChance(5),                                              // 5% critical chance
        criticalMultiplier(2),                                          // 2x critical damage
        attackSpeed(1.0f),                                              // 1 attack per second
        movementSpeed(1.0f),                                            // Normal movement speed
        mana(50),                                                       // Default mana
        maxMana(50),                                                    // Default max mana
        energy(100),                                                    // Default energy
        maxEnergy(100),                                                 // Default max energy
        ammunition(30),                                                 // Default ammunition
        maxAmmunition(30),                                              // Default max ammunition
        timerActive(false),                                             // Timer inactive by default
        timerStart(std::chrono::steady_clock::now()),                   // Current time as start
        timerCurrent(std::chrono::steady_clock::now()),                 // Current time
        totalTimeElapsed(std::chrono::milliseconds(0)),                 // No elapsed time
        bitmapWidth(0),                                                 // No bitmap width set
        bitmapHeight(0),                                                // No bitmap height set
        collisionOffset(Vector2(0.0f, 0.0f)),                           // No collision offset
        equippedWeapon(-1),                                             // No weapon equipped
        equippedArmour(-1),                                             // No armour equipped
#if defined(_USE_NETWORKMANAGER_)
        equippedAccessory(-1),                                          // No accessory equipped
        isNetworkPlayer(false),                                         // Local player by default
        networkSessionID(""),                                           // No network session
        networkLatency(0)                                               // No network latency
#else
        equippedAccessory(-1)                                           // No accessory equipped
#endif
    {
        // Reserve space for common inventory size to avoid frequent reallocations
        inventory.reserve(50);                                          // Reserve space for 50 items
        unlockedAchievements.reserve(20);                               // Reserve space for 20 achievements
        completedQuests.reserve(30);                                    // Reserve space for 30 quests
        discoveredAreas.reserve(100);                                   // Reserve space for 100 areas
    }
};

//==============================================================================
// Game Status Management Class
// Manages overall game state and player session information
//==============================================================================
class GameStatus {
public:
    // Constructor with default initialization
    GameStatus();
    // Destructor
    ~GameStatus();

    // Game State Management
    bool IsGameActive() const { return m_isGameActive.load(); }             // Check if game is actively running
    bool IsGamePaused() const { return m_isGamePaused.load(); }             // Check if game is paused
    bool IsGameTerminated() const { return m_isGameTerminated.load(); }     // Check if game was terminated by user
    bool IsGameInitialized() const { return m_isGameInitialized.load(); }   // Check if game systems are initialized

    // Game State Control Functions
    void StartGame();                                                  // Begin active gameplay
    void PauseGame();                                                  // Pause current gameplay
    void ResumeGame();                                                 // Resume paused gameplay
    void TerminateGame();                                              // Terminate game by user request
    void InitializeGame();                                             // Initialize game systems
    void ShutdownGame();                                               // Shutdown game systems

    // Game Session Information
    GameType GetCurrentGameType() const { return m_currentGameType; }  // Get active game type
    void SetCurrentGameType(GameType gameType);                        // Set active game type
    int GetActivePlayerCount() const { return m_activePlayerCount.load(); } // Get number of active players
    void SetActivePlayerCount(int count);                              // Set number of active players

    // Game Timing Functions
    std::chrono::milliseconds GetGameSessionTime() const;              // Get total game session duration
    std::chrono::milliseconds GetGamePlayTime() const;                 // Get actual gameplay time (excluding pauses)
    void ResetGameTimer();                                             // Reset game session timer

    // Game Difficulty and Settings
    int GetDifficultyLevel() const { return m_difficultyLevel; }       // Get current difficulty setting
    void SetDifficultyLevel(int level);                                // Set game difficulty level
    bool IsNetworkGame() const { return m_isNetworkGame.load(); }      // Check if this is a network game
    void SetNetworkGame(bool isNetwork);                               // Set network game status

    // Game Progress and Statistics
    int GetCurrentLevel() const { return m_currentLevel; }             // Get current game level
    void SetCurrentLevel(int level);                                   // Set current game level
    uint64_t GetTotalScore() const { return m_totalScore.load(); }     // Get combined player scores
    void AddToTotalScore(uint64_t points);                             // Add points to total score

private:
    // Game State Flags
    std::atomic<bool> m_isGameActive;                                  // Game actively running flag
    std::atomic<bool> m_isGamePaused;                                  // Game paused flag
    std::atomic<bool> m_isGameTerminated;                              // Game terminated flag
    std::atomic<bool> m_isGameInitialized;                             // Game initialized flag
    std::atomic<bool> m_isNetworkGame;                                 // Network game flag

    // Game Configuration
    GameType m_currentGameType;                                        // Current active game type
    std::atomic<int> m_activePlayerCount;                              // Number of active players
    int m_difficultyLevel;                                             // Current difficulty setting
    int m_currentLevel;                                                // Current game level
    std::atomic<uint64_t> m_totalScore;                                // Combined total score

    // Game Timing Data
    std::chrono::steady_clock::time_point m_sessionStartTime;          // Game session start time
    std::chrono::steady_clock::time_point m_gamePlayStartTime;         // Gameplay start time
    std::chrono::milliseconds m_totalPauseTime;                        // Total time spent paused
    std::chrono::steady_clock::time_point m_lastPauseTime;             // Last pause timestamp
};

//==============================================================================
// Game Account Management Class
// Manages player account information, DLC access, and platform integration
//==============================================================================
class GameAccount {
public:
    // Constructor and destructor
    GameAccount();
    ~GameAccount();

    // Account Information
    std::string GetAccountID() const { return m_accountID; }           // Get unique account identifier
    std::string GetAccountName() const { return m_accountName; }       // Get account display name
    std::string GetPlatform() const { return m_platform; }             // Get gaming platform (Steam, Epic, etc.)
    bool IsAccountValid() const { return m_isAccountValid.load(); }    // Check if account is valid and authenticated

    // Account Management Functions
    bool LoadAccountData(const std::string& accountID);                // Load account data from storage/server
    bool SaveAccountData();                                            // Save current account data
    void ClearAccountData();                                           // Clear all account information
    bool ValidateAccount();                                            // Validate account with platform/server

    // DLC and Content Access
    bool HasDLCAccess(const std::string& dlcID) const;                 // Check if player has access to specific DLC
    void AddDLCAccess(const std::string& dlcID);                       // Grant access to DLC content
    void RemoveDLCAccess(const std::string& dlcID);                    // Revoke access to DLC content
    std::vector<std::string> GetAvailableDLC() const;                  // Get list of available DLC for this account

    // Achievement and Progress Synchronization
    bool SyncAchievements();                                            // Synchronize achievements with platform
    bool SyncGameProgress();                                            // Synchronize game progress with cloud saves
    bool UploadGameStats();                                             // Upload game statistics to platform

    // Account Statistics
    uint64_t GetTotalPlayTime() const { return m_totalPlayTime.load(); }        // Get lifetime play time in minutes
    uint64_t GetTotalGamesPlayed() const { return m_totalGamesPlayed.load(); }  // Get total games played count
    uint64_t GetLifetimeScore() const { return m_lifetimeScore.load(); }        // Get lifetime accumulated score

    // Platform Integration
    bool ConnectToPlatform(const std::string& platform);                        // Connect to gaming platform API
    void DisconnectFromPlatform();                                              // Disconnect from platform
    bool IsPlatformConnected() const { return m_isPlatformConnected.load(); }   // Check platform connection status

private:
    // Account Identification
    std::string m_accountID;                                            // Unique account identifier
    std::string m_accountName;                                          // Account display name
    std::string m_platform;                                             // Gaming platform identifier
    std::atomic<bool> m_isAccountValid;                                 // Account validation status
    std::atomic<bool> m_isPlatformConnected;                            // Platform connection status

    // DLC and Content Access
    std::vector<std::string> m_ownedDLC;                                // List of owned DLC content
    std::vector<std::string> m_availableContent;                        // List of available content

    // Account Statistics
    std::atomic<uint64_t> m_totalPlayTime;                              // Total lifetime play time in minutes
    std::atomic<uint64_t> m_totalGamesPlayed;                           // Total number of games played
    std::atomic<uint64_t> m_lifetimeScore;                              // Lifetime accumulated score

    // Account Creation and Last Access
    std::chrono::system_clock::time_point m_accountCreated;             // Account creation timestamp
    std::chrono::system_clock::time_point m_lastAccess;                 // Last account access timestamp
};

//==============================================================================
// Main GamePlayer Class
// Comprehensive player management system supporting up to 8 players
//==============================================================================
class GamePlayer {
public:
    // Constructor and destructor
    GamePlayer();
    ~GamePlayer();

    // Initialization and Cleanup
    bool Initialize();                                                  // Initialize player management system
    void Cleanup();                                                     // Clean up all player resources
    bool IsInitialized() const { return m_isInitialized.load(); }       // Check if system is initialized

    // Player Management Functions
    bool InitPlayer(int playerID, const PlayerInfo& playerInfo);        // Initialize specific player with data
    bool RemovePlayer(int playerID);                                    // Remove player from game session
    bool IsPlayerValid(int playerID) const;                             // Check if player ID is valid and active
    PlayerInfo* GetPlayerInfo(int playerID);                            // Get mutable player information
    const PlayerInfo* GetPlayerInfo(int playerID) const;                // Get read-only player information

    // Player Status and State Management
    bool CheckPlayerStatus(int playerID);                               // Check player status and update timers
    bool IsPlayerDead(int playerID) const;                              // Check if player is dead
    bool IsPlayerActive(int playerID) const;                            // Check if player is actively playing
    bool IsDeathAnimationActive(int playerID) const;                    // Check if death animation is playing
    void SetPlayerState(int playerID, PlayerState state);               // Set player activity state
    void StartPlayerTimer(int playerID);                                // Start player event timer
    void StopPlayerTimer(int playerID);                                 // Stop player event timer
    void UpdatePlayerTimer(int playerID);                               // Update player timer calculations

    // Collision Detection System
    bool InitializeCollisionBitmap(int playerID, int width, int height);    // Initialize player collision bitmap
    void ClearCollisionBitmap(int playerID);                                // Clear collision bitmap data
    bool CheckCollisionAtPoint(int playerID, const Vector2& point) const;   // Check collision at specific point
    void SetCollisionPixel(int playerID, const Vector2& point, bool solid); // Set collision pixel state

    // Map and Level Management
    bool LoadTiledMap(const std::string& filename);                         // Load binary tiled map data from file
    bool LoadTiledMapOverlay(const std::string& filename);                  // Load binary tiled map overlay data
    void UnloadTiledMap();                                                  // Unload current tiled map data
    void UnloadTiledMapOverlay();                                           // Unload current tiled map overlay
    bool IsTiledMapLoaded() const { return m_isTiledMapLoaded.load(); }     // Check if tiled map is loaded
    bool IsTiledMapOverlayLoaded() const { return m_isTiledMapOverlayLoaded.load(); } // Check if overlay is loaded

    // Network Communication Functions
    #if defined(_USE_NETWORKMANAGER_)
        bool SendPlayerInfo(int playerID);                                  // Send player information over network
        bool ReceivePlayerInfo(int playerID);                               // Receive player information from network
        bool BroadcastPlayerUpdate(int playerID);                           // Broadcast player update to all clients
        bool HandleNetworkPlayerData(const NetworkPacket& packet);          // Handle incoming network player data
    #endif

    // Game Session Management
    int GetActivePlayerCount() const;                                       // Get number of active players
    std::vector<int> GetActivePlayerIDs() const;                            // Get list of active player IDs
    void UpdateAllPlayers(float deltaTime);                                 // Update all active players with time delta

    // Statistics and Scoring
    uint64_t GetCombinedScore() const;                                      // Get combined score of all players
    PlayerInfo* GetHighestScoringPlayer();                                  // Get player with highest score
    void ResetAllPlayerStats();                                             // Reset all player statistics

    // Game Status and Account Access
    GameStatus& GetGameStatus() { return m_gameStatus; }                    // Get game status manager reference
    const GameStatus& GetGameStatus() const { return m_gameStatus; }        // Get read-only game status reference
    GameAccount& GetGameAccount() { return m_gameAccount; }                 // Get game account manager reference
    const GameAccount& GetGameAccount() const { return m_gameAccount; }     // Get read-only game account reference

private:
    // System State
    std::atomic<bool> m_isInitialized;                                      // System initialization flag
    std::atomic<bool> m_hasCleanedUp;                                       // Cleanup completion flag

    // Player Data Storage (supporting up to 8 players)
    static const int MAX_PLAYERS = 8;                                       // Maximum supported players
    PlayerInfo m_players[MAX_PLAYERS];                                      // Array of player information
    bool m_playerSlotActive[MAX_PLAYERS];                                   // Track which player slots are in use

    // Tiled Map Data
    std::vector<uint8_t> m_tiledMapData;                                    // Binary tiled map data
    std::vector<uint8_t> m_tiledMapOverlayData;                             // Binary tiled map overlay data
    std::atomic<bool> m_isTiledMapLoaded;                                   // Tiled map loaded flag
    std::atomic<bool> m_isTiledMapOverlayLoaded;                            // Tiled map overlay loaded flag
    int m_mapWidth;                                                         // Tiled map width in tiles
    int m_mapHeight;                                                        // Tiled map height in tiles
    int m_tileSize;                                                         // Individual tile size in pixels

    // Game Management Systems
    GameStatus m_gameStatus;                                                // Game status management
    GameAccount m_gameAccount;                                              // Game account management

    // Network Integration Support
    #if defined(_USE_NETWORKMANAGER_)
        bool m_networkEnabled;                                              // Network functionality enabled flag
        std::string m_currentSessionID;                                     // Current network session identifier
    #endif

    // Private Helper Functions
    bool ValidatePlayerID(int playerID) const;                              // Validate player ID range
    void ResetPlayerInfo(int playerID);                                     // Reset player information to defaults
    bool LoadBinaryFile(const std::string& filename, std::vector<uint8_t>& data); // Load binary file into vector
    void UpdatePlayerTimers(int playerID);                                  // Update player timing calculations

    // Collision Detection Helpers
    size_t GetBitmapIndex(int playerID, const Vector2& point) const;        // Get bitmap array index from coordinates
    bool IsValidBitmapCoordinate(int playerID, const Vector2& point) const; // Validate bitmap coordinates

    // Network Communication Helpers
    #if defined(_USE_NETWORKMANAGER_)
        std::vector<uint8_t> SerializePlayerInfo(const PlayerInfo& playerInfo);                 // Serialize player data for network
        bool DeserializePlayerInfo(const std::vector<uint8_t>& data, PlayerInfo& playerInfo);   // Deserialize network player data
        void LogNetworkOperation(const std::string& operation, int playerID);                   // Log network operations for debugging
    #endif
};

// Global GamePlayer instance declaration
extern GamePlayer gamePlayer;

