// -------------------------------------------------------------------------------------------------------------
// GamePlayer.cpp - Implementation of comprehensive game player management system
// Provides complete player management with network integration, collision detection, and multi-platform support
// -------------------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "GamePlayer.h"

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
// Debug flag for GamePlayer class - add this to Debug.h if not present
#endif

// External references required by the GamePlayer system
extern std::shared_ptr<Renderer> renderer;
extern Debug debug;

#if defined(_USE_NETWORKMANAGER_)
    extern NetworkManager networkManager;
    extern PUNPack punPack;
#endif

//==============================================================================
// GameStatus Class Implementation
//==============================================================================

// Constructor - Initialize game status with default values
GameStatus::GameStatus() :
    m_isGameActive(false),                                              // Game not active by default
    m_isGamePaused(false),                                              // Game not paused by default
    m_isGameTerminated(false),                                          // Game not terminated by default
    m_isGameInitialized(false),                                         // Game not initialized by default
    m_isNetworkGame(false),                                             // Not a network game by default
    m_currentGameType(GameType::GT_NONE),                               // No game type set initially
    m_activePlayerCount(0),                                             // No active players initially
    m_difficultyLevel(1),                                               // Default to easy difficulty
    m_currentLevel(1),                                                  // Start at level 1
    m_totalScore(0),                                                    // No score initially
    m_sessionStartTime(std::chrono::steady_clock::now()),               // Record session start time
    m_gamePlayStartTime(std::chrono::steady_clock::now()),              // Record gameplay start time
    m_totalPauseTime(std::chrono::milliseconds(0)),                     // No pause time initially
    m_lastPauseTime(std::chrono::steady_clock::now())                   // Initialize last pause time
{
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus constructor called - initializing game status manager");
    #endif
}

// Destructor - Clean up game status resources
GameStatus::~GameStatus() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus destructor called - cleaning up game status manager");
    #endif

    // Set all flags to safe states for cleanup
    m_isGameActive.store(false);                                        // Mark game as inactive
    m_isGamePaused.store(false);                                        // Clear pause flag
    m_isGameTerminated.store(true);                                     // Mark as terminated
}

// Start active gameplay session
void GameStatus::StartGame() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus::StartGame() called - beginning active gameplay");
    #endif

    // Set game state flags for active gameplay
    m_isGameActive.store(true);                                         // Mark game as actively running
    m_isGamePaused.store(false);                                        // Ensure game is not paused
    m_isGameTerminated.store(false);                                    // Clear termination flag

    // Record gameplay start time for accurate timing calculations
    m_gamePlayStartTime = std::chrono::steady_clock::now();             // Set gameplay start timestamp

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Game successfully started - gameplay is now active");
    #endif
}

// Pause current gameplay session
void GameStatus::PauseGame() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus::PauseGame() called - pausing active gameplay");
    #endif

    // Only pause if game is currently active
    if (m_isGameActive.load()) {
        m_isGamePaused.store(true);                                     // Set pause flag
        m_lastPauseTime = std::chrono::steady_clock::now();             // Record pause start time

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Game successfully paused");
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Attempted to pause game that is not active");
        #endif
    }
}

// Resume paused gameplay session
void GameStatus::ResumeGame() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus::ResumeGame() called - resuming paused gameplay");
    #endif

    // Only resume if game is currently paused and active
    if (m_isGameActive.load() && m_isGamePaused.load()) {
        // Calculate time spent paused and add to total pause time
        auto pauseEndTime = std::chrono::steady_clock::now();           // Get current time
        auto pauseDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            pauseEndTime - m_lastPauseTime);                           // Calculate pause duration
        m_totalPauseTime += pauseDuration;                             // Add to total pause time

        m_isGamePaused.store(false);                                    // Clear pause flag

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Game successfully resumed - pause duration was %lld ms",
                pauseDuration.count());
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Attempted to resume game that is not paused or not active");
        #endif
    }
}

// Terminate game session by user request
void GameStatus::TerminateGame() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus::TerminateGame() called - terminating game by user request");
    #endif

    // Set termination flags
    m_isGameTerminated.store(true);                                     // Mark game as terminated
    m_isGameActive.store(false);                                        // Mark game as inactive
    m_isGamePaused.store(false);                                        // Clear pause flag

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Game successfully terminated by user request");
    #endif
}

// Initialize game systems for new session
void GameStatus::InitializeGame() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus::InitializeGame() called - initializing game systems");
    #endif

    // Reset all game state flags to initial values
    m_isGameActive.store(false);                                        // Game not yet active
    m_isGamePaused.store(false);                                        // Game not paused
    m_isGameTerminated.store(false);                                    // Game not terminated
    m_isGameInitialized.store(true);                                    // Mark as initialized

    // Reset timing information
    m_sessionStartTime = std::chrono::steady_clock::now();              // Set new session start time
    m_gamePlayStartTime = std::chrono::steady_clock::now();             // Set new gameplay start time
    m_totalPauseTime = std::chrono::milliseconds(0);                    // Reset total pause time

    // Reset game progress
    m_totalScore.store(0);                                              // Reset total score

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Game systems successfully initialized");
    #endif
}

// Shutdown game systems and clean up resources
void GameStatus::ShutdownGame() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus::ShutdownGame() called - shutting down game systems");
    #endif

    // Set all flags to shutdown state
    m_isGameActive.store(false);                                        // Mark game as inactive
    m_isGamePaused.store(false);                                        // Clear pause flag
    m_isGameTerminated.store(true);                                     // Mark as terminated
    m_isGameInitialized.store(false);                                   // Mark as not initialized

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Game systems successfully shut down");
    #endif
}

// Set the current active game type
void GameStatus::SetCurrentGameType(GameType gameType) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameStatus::SetCurrentGameType() called - setting game type to 0x%08X",
            static_cast<uint32_t>(gameType));
    #endif

    m_currentGameType = gameType;                                       // Store new game type

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Game type successfully updated");
    #endif
}

// Set the number of active players
void GameStatus::SetActivePlayerCount(int count) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameStatus::SetActivePlayerCount() called - setting count to %d", count);
    #endif

    // Validate player count range
    if (count >= 0 && count <= 8) {                                     // Ensure valid range (0-8 players)
        m_activePlayerCount.store(count);                               // Store new player count

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Active player count successfully set to %d", count);
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid player count %d - must be between 0 and 8", count);
        #endif
    }
}

// Get total game session duration
std::chrono::milliseconds GameStatus::GetGameSessionTime() const {
    auto currentTime = std::chrono::steady_clock::now();                // Get current timestamp
    auto sessionDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - m_sessionStartTime);                             // Calculate total session time

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Game session time: %lld ms", sessionDuration.count());
    #endif

    return sessionDuration;                                             // Return total session duration
}

// Get actual gameplay time excluding pauses
std::chrono::milliseconds GameStatus::GetGamePlayTime() const {
    auto currentTime = std::chrono::steady_clock::now();                // Get current timestamp
    auto totalGameTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - m_gamePlayStartTime);                            // Calculate total time since gameplay start

    // Subtract total pause time to get actual gameplay time
    auto actualGamePlayTime = totalGameTime - m_totalPauseTime;         // Remove pause time from total

    // If currently paused, subtract current pause duration
    if (m_isGamePaused.load()) {
        auto currentPauseDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - m_lastPauseTime);                            // Calculate current pause duration
        actualGamePlayTime -= currentPauseDuration;                    // Subtract from gameplay time
    }

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Actual gameplay time: %lld ms", actualGamePlayTime.count());
    #endif

    return actualGamePlayTime;                                          // Return actual gameplay duration
}

// Reset game session timer to current time
void GameStatus::ResetGameTimer() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameStatus::ResetGameTimer() called - resetting game timers");
    #endif

    auto currentTime = std::chrono::steady_clock::now();                // Get current timestamp
    m_sessionStartTime = currentTime;                                   // Reset session start time
    m_gamePlayStartTime = currentTime;                                  // Reset gameplay start time
    m_totalPauseTime = std::chrono::milliseconds(0);                    // Reset total pause time
    m_lastPauseTime = currentTime;                                      // Reset last pause time

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Game timers successfully reset");
    #endif
}

// Set game difficulty level
void GameStatus::SetDifficultyLevel(int level) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameStatus::SetDifficultyLevel() called - setting level to %d", level);
    #endif

    // Validate difficulty level range (typically 1-10)
    if (level >= 1 && level <= 10) {                                    // Ensure valid difficulty range
        m_difficultyLevel = level;                                      // Store new difficulty level

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Difficulty level successfully set to %d", level);
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid difficulty level %d - must be between 1 and 10", level);
        #endif
    }
}

// Set network game status
void GameStatus::SetNetworkGame(bool isNetwork) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameStatus::SetNetworkGame() called - setting network status to %s",
            isNetwork ? L"true" : L"false");
    #endif

#if defined(_USE_NETWORKMANAGER_)
    m_isNetworkGame.store(isNetwork);                                   // Store network game status
#else
    isNetwork = false;
    m_isNetworkGame.store(isNetwork);                                   // OVERRIDE IF NOT A NETWORKED GAME
#endif

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Network game status successfully updated");
    #endif
}

// Set current game level
void GameStatus::SetCurrentLevel(int level) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameStatus::SetCurrentLevel() called - setting level to %d", level);
    #endif

    // Validate level range (typically 1 or higher)
    if (level >= 1) {                                                   // Ensure valid level number
        m_currentLevel = level;                                         // Store new level

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Current level successfully set to %d", level);
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid level %d - must be 1 or higher", level);
        #endif
    }
}

// Add points to total game score
void GameStatus::AddToTotalScore(uint64_t points) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GameStatus::AddToTotalScore() called - adding %llu points", points);
    #endif

    m_totalScore.fetch_add(points);                                     // Atomically add points to total score

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Total score is now %llu", m_totalScore.load());
    #endif
}

//==============================================================================
// GameAccount Class Implementation
//==============================================================================

// Constructor - Initialize game account with default values
GameAccount::GameAccount() :
    m_accountID(""),                                                    // No account ID initially
    m_accountName(""),                                                  // No account name initially
    m_platform(""),                                                     // No platform initially
    m_isAccountValid(false),                                            // Account not valid by default
    m_isPlatformConnected(false),                                       // Not connected to platform
    m_totalPlayTime(0),                                                 // No play time initially
    m_totalGamesPlayed(0),                                              // No games played initially
    m_lifetimeScore(0),                                                 // No lifetime score initially
    m_accountCreated(std::chrono::system_clock::now()),                 // Set creation time to now
    m_lastAccess(std::chrono::system_clock::now())                      // Set last access to now
{
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount constructor called - initializing account manager");
    #endif

    // Reserve space for common DLC collections to avoid frequent reallocations
    m_ownedDLC.reserve(20);                                             // Reserve space for 20 DLC items
    m_availableContent.reserve(50);                                     // Reserve space for 50 content items
}

// Destructor - Clean up game account resources
GameAccount::~GameAccount() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount destructor called - cleaning up account manager");
    #endif

    // Disconnect from platform if connected
    if (m_isPlatformConnected.load()) {
        DisconnectFromPlatform();                                       // Clean disconnect from platform
    }

    // Clear sensitive account data
    ClearAccountData();                                                 // Clear all account information
}

// Load account data from storage or server
bool GameAccount::LoadAccountData(const std::string& accountID) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameAccount::LoadAccountData() called for account: %S", accountID.c_str());
    #endif

    // Validate account ID parameter
    if (accountID.empty()) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot load account data - account ID is empty");
        #endif
        return false;                                                   // Cannot load with empty ID
    }

    try {
        // Store account ID for future reference
        m_accountID = accountID;                                        // Set account identifier

        // TODO: Implement actual account data loading from storage/server
        // This would typically involve reading from a configuration file,
        // local database, or making a network request to a game server

        // For now, set basic default values
        m_accountName = "Player_" + accountID;                          // Generate default account name
        m_isAccountValid.store(true);                                   // Mark account as valid
        m_lastAccess = std::chrono::system_clock::now();                // Update last access time

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Account data successfully loaded for: %S", m_accountName.c_str());
        #endif

        return true;                                                    // Account loaded successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception loading account data: %S", e.what());
        #endif
        return false;                                                   // Failed to load account
    }
}

// Save current account data to storage
bool GameAccount::SaveAccountData() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount::SaveAccountData() called");
    #endif

    // Validate that account is valid before saving
    if (!m_isAccountValid.load() || m_accountID.empty()) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot save account data - account is not valid or ID is empty");
        #endif
        return false;                                                   // Cannot save invalid account
    }

    try {
        // TODO: Implement actual account data saving to storage/server
        // This would typically involve writing to a configuration file,
        // local database, or making a network request to a game server

        // Update last access time
        m_lastAccess = std::chrono::system_clock::now();                // Set current time as last access

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Account data successfully saved for: %S", m_accountName.c_str());
        #endif

        return true;                                                    // Account saved successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception saving account data: %S", e.what());
        #endif
        return false;                                                   // Failed to save account
    }
}

// Clear all account information
void GameAccount::ClearAccountData() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount::ClearAccountData() called - clearing all account data");
    #endif

    // Clear account identification
    m_accountID.clear();                                                // Clear account ID
    m_accountName.clear();                                              // Clear account name
    m_platform.clear();                                                 // Clear platform identifier

    // Reset account status flags
    m_isAccountValid.store(false);                                      // Mark account as invalid
    m_isPlatformConnected.store(false);                                 // Mark as disconnected from platform

    // Clear DLC and content data
    m_ownedDLC.clear();                                                 // Clear owned DLC list
    m_availableContent.clear();                                         // Clear available content list

    // Reset statistics
    m_totalPlayTime.store(0);                                           // Reset total play time
    m_totalGamesPlayed.store(0);                                        // Reset games played count
    m_lifetimeScore.store(0);                                           // Reset lifetime score

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Account data successfully cleared");
    #endif
}

// Validate account with platform or server
bool GameAccount::ValidateAccount() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount::ValidateAccount() called");
    #endif

    // Check if account ID is present
    if (m_accountID.empty()) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot validate account - account ID is empty");
        #endif
        return false;                                                   // Cannot validate without ID
    }

    try {
        // TODO: Implement actual account validation with platform/server
        // This would typically involve making a network request to verify
        // the account credentials and permissions

        // For now, perform basic validation
        bool isValid = !m_accountID.empty() && !m_accountName.empty();  // Basic validation check
        m_isAccountValid.store(isValid);                                // Store validation result

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Account validation result: %s", isValid ? L"valid" : L"invalid");
        #endif

        return isValid;                                                 // Return validation result
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during account validation: %S", e.what());
        #endif
        m_isAccountValid.store(false);                                  // Mark as invalid on exception
        return false;                                                   // Failed validation
    }
}

// Check if player has access to specific DLC
bool GameAccount::HasDLCAccess(const std::string& dlcID) const {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GameAccount::HasDLCAccess() called for DLC: %S", dlcID.c_str());
    #endif

    // Search for DLC ID in owned DLC list
    auto it = std::find(m_ownedDLC.begin(), m_ownedDLC.end(), dlcID);   // Search for DLC in owned list
    bool hasAccess = (it != m_ownedDLC.end());                         // Check if DLC was found

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"DLC access check result: %s", hasAccess ? L"granted" : L"denied");
    #endif

    return hasAccess;                                                   // Return access result
}

// Grant access to DLC content
void GameAccount::AddDLCAccess(const std::string& dlcID) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameAccount::AddDLCAccess() called for DLC: %S", dlcID.c_str());
    #endif

    // Check if DLC is already owned
    if (!HasDLCAccess(dlcID)) {
        m_ownedDLC.push_back(dlcID);                                    // Add DLC to owned list

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DLC access granted: %S", dlcID.c_str());
    #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"DLC %S already owned", dlcID.c_str());
        #endif
    }
}

// Revoke access to DLC content
void GameAccount::RemoveDLCAccess(const std::string& dlcID) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameAccount::RemoveDLCAccess() called for DLC: %S", dlcID.c_str());
    #endif

    // Find and remove DLC from owned list
    auto it = std::find(m_ownedDLC.begin(), m_ownedDLC.end(), dlcID);   // Search for DLC in owned list
    if (it != m_ownedDLC.end()) {
        m_ownedDLC.erase(it);                                           // Remove DLC from list

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"DLC access revoked: %S", dlcID.c_str());
    #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"DLC %S not found in owned list", dlcID.c_str());
        #endif
    }
}

// Get list of available DLC for this account
std::vector<std::string> GameAccount::GetAvailableDLC() const {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GameAccount::GetAvailableDLC() called - returning %zu DLC items",
            m_ownedDLC.size());
    #endif

    return m_ownedDLC;                                                  // Return copy of owned DLC list
}

// Synchronize achievements with platform
bool GameAccount::SyncAchievements() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount::SyncAchievements() called");
    #endif

    // Check if connected to platform
    if (!m_isPlatformConnected.load()) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot sync achievements - not connected to platform");
        #endif
        return false;                                                   // Cannot sync without platform connection
    }

    try {
        // TODO: Implement actual achievement synchronization with platform
        // This would typically involve API calls to Steam, Epic Games Store, etc.

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Achievement synchronization completed successfully");
        #endif

        return true;                                                    // Sync completed successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during achievement sync: %S", e.what());
        #endif
        return false;                                                   // Failed to sync achievements
    }
}

// Synchronize game progress with cloud saves
bool GameAccount::SyncGameProgress() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount::SyncGameProgress() called");
    #endif

    // Check if connected to platform
    if (!m_isPlatformConnected.load()) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot sync game progress - not connected to platform");
        #endif
        return false;                                                   // Cannot sync without platform connection
    }

    try {
        // TODO: Implement actual game progress synchronization with cloud saves
        // This would typically involve uploading/downloading save files

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Game progress synchronization completed successfully");
        #endif

        return true;                                                    // Sync completed successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during game progress sync: %S", e.what());
        #endif
        return false;                                                   // Failed to sync game progress
    }
}

// Upload game statistics to platform
bool GameAccount::UploadGameStats() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount::UploadGameStats() called");
    #endif

    // Check if connected to platform
    if (!m_isPlatformConnected.load()) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot upload game stats - not connected to platform");
        #endif
        return false;                                                   // Cannot upload without platform connection
    }

    try {
        // TODO: Implement actual game statistics upload to platform
        // This would typically involve API calls to submit play time,
        // scores, and other statistical data

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Game statistics upload completed successfully");
        #endif

        return true;                                                    // Upload completed successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during game stats upload: %S", e.what());
        #endif
        return false;                                                   // Failed to upload statistics
    }
}

// Connect to gaming platform API
bool GameAccount::ConnectToPlatform(const std::string& platform) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GameAccount::ConnectToPlatform() called for platform: %S", platform.c_str());
    #endif

    // Validate platform parameter
    if (platform.empty()) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot connect to platform - platform name is empty");
        #endif
        return false;                                                   // Cannot connect with empty platform name
    }

    try {
        // TODO: Implement actual platform connection logic
        // This would typically involve initializing platform-specific APIs
        // such as Steam API, Epic Games Store API, etc.

        m_platform = platform;                                          // Store platform identifier
        m_isPlatformConnected.store(true);                              // Mark as connected

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Successfully connected to platform: %S", platform.c_str());
        #endif

        return true;                                                    // Connection successful
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception connecting to platform: %S", e.what());
        #endif
        m_isPlatformConnected.store(false);                             // Mark as disconnected on error
        return false;                                                   // Failed to connect
    }
}

// Disconnect from gaming platform
void GameAccount::DisconnectFromPlatform() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GameAccount::DisconnectFromPlatform() called");
    #endif

    try {
        // TODO: Implement actual platform disconnection logic
        // This would typically involve cleaning up platform-specific APIs

        m_isPlatformConnected.store(false);                             // Mark as disconnected
        m_platform.clear();                                             // Clear platform identifier

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully disconnected from platform");
        #endif
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception disconnecting from platform: %S", e.what());
        #endif
    }
}

//==============================================================================
// GamePlayer Class Implementation
//==============================================================================

// Constructor - Initialize game player management system
GamePlayer::GamePlayer() :
    m_isInitialized(false),                                             // System not initialized by default
    m_hasCleanedUp(false),                                              // Cleanup not performed by default
    m_isTiledMapLoaded(false),                                          // No tiled map loaded initially
    m_isTiledMapOverlayLoaded(false),                                   // No tiled map overlay loaded initially
    m_mapWidth(0),                                                      // No map width set initially
    m_mapHeight(0),                                                     // No map height set initially
    m_tileSize(32),                                                     // Default tile size of 32 pixels
    m_gameStatus(),                                                     // Initialize game status manager
    m_gameAccount()                                                     // Initialize game account manager
{
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamePlayer constructor called - initializing player management system");
#endif

    // Initialize all player slots as inactive
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        m_playerSlotActive[i] = false;                                  // Mark all slots as inactive
        ResetPlayerInfo(i);                                             // Reset player information to defaults
    }

    // Reserve space for tiled map data to avoid frequent reallocations
    m_tiledMapData.reserve(1024 * 1024);                               // Reserve 1MB for tiled map data
    m_tiledMapOverlayData.reserve(512 * 1024);                         // Reserve 512KB for overlay data

#if defined(_USE_NETWORKMANAGER_)
    m_networkEnabled = false;                                       // Network functionality disabled by default
    m_currentSessionID.clear();                                     // Clear session identifier
#endif
}

// Destructor - Clean up game player management system
GamePlayer::~GamePlayer() {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamePlayer destructor called - cleaning up player management system");
#endif

    // Perform cleanup if not already done
    if (!m_hasCleanedUp.load()) {
        Cleanup();                                                      // Ensure proper cleanup
    }
}

// Initialize player management system
bool GamePlayer::Initialize() {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamePlayer::Initialize() called - initializing player management system");
#endif

    // Prevent double initialization
    if (m_isInitialized.load()) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Player management system already initialized");
#endif
        return true;                                                    // Already initialized
    }

    try {
        // Initialize game status manager
        m_gameStatus.InitializeGame();                                  // Initialize game status system

        // Reset all player data to default values
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            ResetPlayerInfo(i);                                         // Reset each player slot
            m_playerSlotActive[i] = false;                              // Mark slot as inactive
        }

        // Clear tiled map data
        m_tiledMapData.clear();                                         // Clear any existing map data
        m_tiledMapOverlayData.clear();                                  // Clear any existing overlay data
        m_isTiledMapLoaded.store(false);                                // Mark map as not loaded
        m_isTiledMapOverlayLoaded.store(false);                         // Mark overlay as not loaded

        #if defined(_USE_NETWORKMANAGER_)
            // Initialize network functionality if available
            if (networkManager.IsInitialized()) {
                m_networkEnabled = true;                                // Enable network functionality
                m_currentSessionID = "LOCAL_SESSION";                   // Set default session ID

                #if defined(_DEBUG_NETWORKMANAGER_)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Network functionality enabled for GamePlayer");
                #endif
            }
            else {
                m_networkEnabled = false;                               // Disable network functionality
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Network functionality disabled - NetworkManager not initialized");
            }
        #endif

        // Mark system as initialized
        m_isInitialized.store(true);                                    // Set initialization flag
        m_hasCleanedUp.store(false);                                    // Clear cleanup flag

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Player management system successfully initialized");
#endif

        return true;                                                    // Initialization successful
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during GamePlayer initialization: %S", e.what());
#endif
        return false;                                                   // Initialization failed
    }
}

// Clean up all player resources
void GamePlayer::Cleanup() {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamePlayer::Cleanup() called - cleaning up player management system");
#endif

    // Prevent double cleanup
    if (m_hasCleanedUp.load()) {
        return;                                                         // Already cleaned up
    }

    try {
        // Clean up all player data
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (m_playerSlotActive[i]) {
                RemovePlayer(i);                                        // Remove active players
            }
            ResetPlayerInfo(i);                                         // Reset player information
        }

        // Unload tiled map data
        UnloadTiledMap();                                               // Unload tiled map
        UnloadTiledMapOverlay();                                        // Unload tiled map overlay

        // Shutdown game status and account managers
        m_gameStatus.ShutdownGame();                                    // Shutdown game status system

        #if defined(_USE_NETWORKMANAGER_)
            // Clean up network functionality
            m_networkEnabled = false;                                   // Disable network functionality
            m_currentSessionID.clear();                                 // Clear session identifier
        #endif

        // Mark system as cleaned up
        m_isInitialized.store(false);                                   // Clear initialization flag
        m_hasCleanedUp.store(true);                                     // Set cleanup flag

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Player management system successfully cleaned up");
        #endif
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception during GamePlayer cleanup: %S", e.what());
        #endif
    }
}

// Initialize specific player with data
bool GamePlayer::InitPlayer(int playerID, const PlayerInfo& playerInfo) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::InitPlayer() called for player %d", playerID);
    #endif

    // Validate player ID
    if (!ValidatePlayerID(playerID)) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid player ID %d - must be between 0 and %d",
                playerID, MAX_PLAYERS - 1);
        #endif
        return false;                                                   // Invalid player ID
    }

    // Check if system is initialized
    if (!m_isInitialized.load()) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot initialize player - system not initialized");
        #endif
        return false;                                                   // System not initialized
    }

    try {
        // Copy player information
        m_players[playerID] = playerInfo;                               // Copy provided player data
        m_players[playerID].playerID = playerID;                        // Ensure correct player ID

        // Initialize collision bitmap with renderer dimensions
        if (renderer) {
            InitializeCollisionBitmap(playerID, renderer->iOrigWidth, renderer->iOrigHeight);
        }

        // Set player slot as active
        m_playerSlotActive[playerID] = true;                            // Mark slot as active

        // Start player timer if specified
        if (m_players[playerID].timerActive) {
            StartPlayerTimer(playerID);                                 // Start event timer
        }

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Player %d (%S) successfully initialized",
                playerID, std::wstring(playerInfo.playerName.begin(), playerInfo.playerName.end()).c_str());
        #endif

        return true;                                                    // Player initialized successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception initializing player %d: %S", playerID, e.what());
        #endif
        return false;                                                   // Failed to initialize player
    }
}

// Remove player from game session
bool GamePlayer::RemovePlayer(int playerID) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::RemovePlayer() called for player %d", playerID);
    #endif

    // Validate player ID
    if (!ValidatePlayerID(playerID)) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid player ID %d", playerID);
        #endif
        return false;                                                   // Invalid player ID
    }

    // Check if player slot is active
    if (!m_playerSlotActive[playerID]) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Player %d is not active", playerID);
        #endif
        return false;                                                   // Player not active
    }

    try {
        // Stop player timer if active
        if (m_players[playerID].timerActive) {
            StopPlayerTimer(playerID);                                  // Stop event timer
        }

        // Clear collision bitmap
        ClearCollisionBitmap(playerID);                                 // Clear collision data

        // Reset player information
        ResetPlayerInfo(playerID);                                      // Reset to default values

        // Mark slot as inactive
        m_playerSlotActive[playerID] = false;                           // Mark slot as inactive

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Player %d successfully removed", playerID);
        #endif

        return true;                                                    // Player removed successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception removing player %d: %S", playerID, e.what());
        #endif
        return false;                                                   // Failed to remove player
    }
}

// Check if player ID is valid and active
bool GamePlayer::IsPlayerValid(int playerID) const {
    // Validate player ID range and check if slot is active
    return ValidatePlayerID(playerID) && m_playerSlotActive[playerID];
}

// Get mutable player information
PlayerInfo* GamePlayer::GetPlayerInfo(int playerID) {
    // Validate player and return pointer to player data
    if (IsPlayerValid(playerID)) {
        return &m_players[playerID];                                    // Return pointer to player data
    }
    return nullptr;                                                     // Invalid player or not active
}

// Get read-only player information
const PlayerInfo* GamePlayer::GetPlayerInfo(int playerID) const {
    // Validate player and return const pointer to player data
    if (IsPlayerValid(playerID)) {
        return &m_players[playerID];                                    // Return const pointer to player data
    }
    return nullptr;                                                     // Invalid player or not active
}

// Check player status and update timers
bool GamePlayer::CheckPlayerStatus(int playerID) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GamePlayer::CheckPlayerStatus() called for player %d", playerID);
    #endif

    // Validate player
    if (!IsPlayerValid(playerID)) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid player ID %d for status check", playerID);
        #endif
        return false;                                                   // Invalid player
    }

    try {
        PlayerInfo& player = m_players[playerID];                       // Get reference to player data

        // Update player timer if active
        if (player.timerActive) {
            UpdatePlayerTimers(playerID);                               // Update timing calculations
        }

        // Check if player is dead and no death animation is playing
        bool statusOK = !player.isDead || player.deathAnimation == DeathAnimationState::NONE;

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %d status check: %s (Dead: %s, Animation: %d)",
                playerID, statusOK ? L"OK" : L"Not OK", player.isDead ? L"Yes" : L"No",
                static_cast<int>(player.deathAnimation));
        #endif

        return statusOK;                                                // Return status check result
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception checking player %d status: %S", playerID, e.what());
        #endif
        return false;                                                   // Failed status check
    }
}

// Check if player is dead
bool GamePlayer::IsPlayerDead(int playerID) const {
    // Validate player and return death status
    if (IsPlayerValid(playerID)) {
        return m_players[playerID].isDead;                              // Return death status
    }
    return false;                                                       // Invalid player assumed alive
}

// Check if player is actively playing
bool GamePlayer::IsPlayerActive(int playerID) const {
    // Validate player and return active status
    if (IsPlayerValid(playerID)) {
        return m_players[playerID].isActive &&
            m_players[playerID].currentState == PlayerState::ACTIVE; // Return active status
    }
    return false;                                                       // Invalid player not active
}

// Check if death animation is playing
bool GamePlayer::IsDeathAnimationActive(int playerID) const {
    // Validate player and return death animation status
    if (IsPlayerValid(playerID)) {
        return m_players[playerID].deathAnimation != DeathAnimationState::NONE; // Return animation status
    }
    return false;                                                       // Invalid player has no animation
}

// Set player activity state
void GamePlayer::SetPlayerState(int playerID, PlayerState state) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::SetPlayerState() called for player %d, state %d",
            playerID, static_cast<int>(state));
    #endif

    // Validate player
    if (IsPlayerValid(playerID)) {
        m_players[playerID].currentState = state;                       // Set new player state

        // Update activity flag based on state
        m_players[playerID].isActive = (state == PlayerState::ACTIVE);  // Set active flag

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Player %d state successfully set to %d",
                playerID, static_cast<int>(state));
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Cannot set state for invalid player %d", playerID);
        #endif
    }
}

// Start player event timer
void GamePlayer::StartPlayerTimer(int playerID) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::StartPlayerTimer() called for player %d", playerID);
    #endif

    // Validate player
    if (IsPlayerValid(playerID)) {
        PlayerInfo& player = m_players[playerID];                       // Get reference to player data

        // Set timer start time and activate timer
        player.timerStart = std::chrono::steady_clock::now();           // Set timer start time
        player.timerCurrent = player.timerStart;                       // Initialize current time
        player.totalTimeElapsed = std::chrono::milliseconds(0);        // Reset elapsed time
        player.timerActive = true;                                      // Activate timer

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Timer started for player %d", playerID);
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Cannot start timer for invalid player %d", playerID);
        #endif
    }
}

// Stop player event timer
void GamePlayer::StopPlayerTimer(int playerID) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::StopPlayerTimer() called for player %d", playerID);
    #endif

    // Validate player
    if (IsPlayerValid(playerID)) {
        PlayerInfo& player = m_players[playerID];                       // Get reference to player data

        // Update final elapsed time before stopping
        if (player.timerActive) {
            UpdatePlayerTimers(playerID);                               // Final time update
        }

        // Deactivate timer
        player.timerActive = false;                                     // Deactivate timer

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Timer stopped for player %d - total elapsed: %lld ms",
                playerID, player.totalTimeElapsed.count());
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Cannot stop timer for invalid player %d", playerID);
        #endif
    }
}

// Update player timer calculations
void GamePlayer::UpdatePlayerTimer(int playerID) {
    // Validate player and update timers
    if (IsPlayerValid(playerID)) {
        UpdatePlayerTimers(playerID);                                   // Update timing calculations
    }
}

// Initialize player collision bitmap
bool GamePlayer::InitializeCollisionBitmap(int playerID, int width, int height) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::InitializeCollisionBitmap() called for player %d (%dx%d)",
            playerID, width, height);
    #endif

    // Validate player
    if (!IsPlayerValid(playerID)) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid player ID %d for collision bitmap", playerID);
        #endif
        return false;                                                   // Invalid player
    }

    // Validate dimensions
    if (width <= 0 || height <= 0) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid bitmap dimensions %dx%d", width, height);
        #endif
        return false;                                                   // Invalid dimensions
    }

    try {
        PlayerInfo& player = m_players[playerID];                       // Get reference to player data

        // Calculate bitmap size in bytes
        size_t bitmapSize = static_cast<size_t>(width) * static_cast<size_t>(height);

        // Initialize collision bitmap
        player.collisionBitmap.resize(bitmapSize, 0);                   // Resize and clear bitmap
        player.bitmapWidth = width;                                     // Set bitmap width
        player.bitmapHeight = height;                                   // Set bitmap height
        player.collisionOffset = Vector2(0.0f, 0.0f);                   // Reset collision offset

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Collision bitmap initialized for player %d (%zu bytes)",
                playerID, bitmapSize);
        #endif

        return true;                                                    // Bitmap initialized successfully
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception initializing collision bitmap for player %d: %S",
                playerID, e.what());
        #endif
        return false;                                                   // Failed to initialize bitmap
    }
}

// Clear collision bitmap data
void GamePlayer::ClearCollisionBitmap(int playerID) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::ClearCollisionBitmap() called for player %d", playerID);
    #endif

    // Validate player
    if (IsPlayerValid(playerID)) {
        PlayerInfo& player = m_players[playerID];                       // Get reference to player data

        // Clear collision bitmap data
        std::fill(player.collisionBitmap.begin(), player.collisionBitmap.end(), 0); // Clear all bitmap data

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Collision bitmap cleared for player %d", playerID);
        #endif
    }
    else {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Cannot clear collision bitmap for invalid player %d", playerID);
        #endif
    }
}

// Check collision at specific point
bool GamePlayer::CheckCollisionAtPoint(int playerID, const Vector2& point) const {
    // Validate player
    if (!IsPlayerValid(playerID)) {
        return false;                                                   // Invalid player has no collision
    }

    const PlayerInfo& player = m_players[playerID];                     // Get const reference to player data

    // Validate bitmap coordinates
    if (!IsValidBitmapCoordinate(playerID, point)) {
        return false;                                                   // Invalid coordinates
    }

    // Get bitmap index and check collision value
    size_t index = GetBitmapIndex(playerID, point);                     // Get bitmap array index
    return player.collisionBitmap[index] != 0;                         // Return collision status
}

// Set collision pixel state
void GamePlayer::SetCollisionPixel(int playerID, const Vector2& point, bool solid) {
    // Validate player
    if (!IsPlayerValid(playerID)) {
        return;                                                         // Invalid player
    }

    // Validate bitmap coordinates
    if (!IsValidBitmapCoordinate(playerID, point)) {
        return;                                                         // Invalid coordinates
    }

    PlayerInfo& player = m_players[playerID];                           // Get reference to player data

    // Set collision pixel value
    size_t index = GetBitmapIndex(playerID, point);                     // Get bitmap array index
    player.collisionBitmap[index] = solid ? 1 : 0;                     // Set collision value
}

// Load binary tiled map data from file
bool GamePlayer::LoadTiledMap(const std::string& filename) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::LoadTiledMap() called for file: %S", filename.c_str());
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot load tiled map - system not initialized");
#endif
        return false;                                                   // System not initialized
    }

    try {
        // Load binary map data from file
        if (!LoadBinaryFile(filename, m_tiledMapData)) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to load tiled map file: %S", filename.c_str());
#endif
            return false;                                               // Failed to load file
        }

        // Mark tiled map as loaded
        m_isTiledMapLoaded.store(true);                                 // Set loaded flag

        // TODO: Parse map header to extract map dimensions and tile size
        // This would typically involve reading a header structure from the binary data

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Tiled map successfully loaded (%zu bytes)", m_tiledMapData.size());
#endif

        return true;                                                    // Map loaded successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception loading tiled map: %S", e.what());
#endif
        return false;                                                   // Failed to load map
    }
}

// Load binary tiled map overlay data
bool GamePlayer::LoadTiledMapOverlay(const std::string& filename) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::LoadTiledMapOverlay() called for file: %S", filename.c_str());
#endif

    // Check if system is initialized
    if (!m_isInitialized.load()) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot load tiled map overlay - system not initialized");
#endif
        return false;                                                   // System not initialized
    }

    try {
        // Load binary overlay data from file
        if (!LoadBinaryFile(filename, m_tiledMapOverlayData)) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to load tiled map overlay file: %S", filename.c_str());
#endif
            return false;                                               // Failed to load file
        }

        // Mark tiled map overlay as loaded
        m_isTiledMapOverlayLoaded.store(true);                          // Set loaded flag

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Tiled map overlay successfully loaded (%zu bytes)",
            m_tiledMapOverlayData.size());
#endif

        return true;                                                    // Overlay loaded successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception loading tiled map overlay: %S", e.what());
#endif
        return false;                                                   // Failed to load overlay
    }
}

// Unload current tiled map data
void GamePlayer::UnloadTiledMap() {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamePlayer::UnloadTiledMap() called - unloading tiled map data");
#endif

    // Clear tiled map data
    m_tiledMapData.clear();                                             // Clear map data vector
    m_tiledMapData.shrink_to_fit();                                     // Free allocated memory
    m_isTiledMapLoaded.store(false);                                    // Mark as not loaded

    // Reset map dimensions
    m_mapWidth = 0;                                                     // Reset map width
    m_mapHeight = 0;                                                    // Reset map height

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Tiled map data successfully unloaded");
#endif
}

// Unload current tiled map overlay
void GamePlayer::UnloadTiledMapOverlay() {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamePlayer::UnloadTiledMapOverlay() called - unloading tiled map overlay data");
#endif

    // Clear tiled map overlay data
    m_tiledMapOverlayData.clear();                                      // Clear overlay data vector
    m_tiledMapOverlayData.shrink_to_fit();                              // Free allocated memory
    m_isTiledMapOverlayLoaded.store(false);                             // Mark as not loaded

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Tiled map overlay data successfully unloaded");
#endif
}

#if defined(_USE_NETWORKMANAGER_)
// Send player information over network
bool GamePlayer::SendPlayerInfo(int playerID) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::SendPlayerInfo() called for player %d", playerID);

    // Validate player
    if (!IsPlayerValid(playerID)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Cannot send info for invalid player %d", playerID);
        return false;                                                   // Invalid player
    }

    // Check if network functionality is enabled
    if (!m_networkEnabled) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot send player info - network functionality disabled");
        return false;                                                   // Network not enabled
    }

    // Check if network manager is connected
    if (!networkManager.IsConnected()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot send player info - not connected to server");
        return false;                                                   // Not connected to server
    }

    try {
        const PlayerInfo& player = m_players[playerID];                 // Get const reference to player data

        // Serialize player information for network transmission
        std::vector<uint8_t> serializedData = SerializePlayerInfo(player); // Serialize player data

        // Send player information via network manager
        if (!networkManager.SendPacket(NetworkCommand::CMD_PLAYER_ACTION, serializedData)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to send player %d info over network", playerID);
            return false;                                               // Failed to send packet
        }

        // Log network operation for debugging
        LogNetworkOperation("SendPlayerInfo", playerID);               // Log operation

        debug.logDebugMessage(LogLevel::LOG_INFO, L"Player %d info successfully sent over network (%zu bytes)",
            playerID, serializedData.size());

        return true;                                                    // Player info sent successfully
    }
    catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception sending player %d info: %S", playerID, e.what());
        return false;                                                   // Failed to send player info
    }
}

// Receive player information from network
bool GamePlayer::ReceivePlayerInfo(int playerID) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::ReceivePlayerInfo() called for player %d", playerID);

    // Validate player ID range (allow initialization of new players)
    if (!ValidatePlayerID(playerID)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid player ID %d for receive operation", playerID);
        return false;                                                   // Invalid player ID
    }

    // Check if network functionality is enabled
    if (!m_networkEnabled) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot receive player info - network functionality disabled");
        return false;                                                   // Network not enabled
    }

    // Check if network manager has pending packets
    if (!networkManager.HasPendingPackets()) {
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"No pending network packets for player info");
        return false;                                                   // No packets available
    }

    try {
        // Get next packet from network manager
        NetworkPacket packet = networkManager.GetNextPacket();         // Get network packet

        // Check if packet is player action data
        if (packet.header.command != NetworkCommand::CMD_PLAYER_ACTION) {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Received non-player packet (command: 0x%X)",
                static_cast<uint32_t>(packet.header.command));
            return false;                                               // Not a player packet
        }

        // Deserialize player information from packet data
        PlayerInfo receivedPlayerInfo;                                  // Create player info structure
        if (!DeserializePlayerInfo(packet.data, receivedPlayerInfo)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to deserialize player info from network packet");
            return false;                                               // Failed to deserialize
        }

        // Update player information with received data
        if (!m_playerSlotActive[playerID]) {
            // Initialize new network player
            if (!InitPlayer(playerID, receivedPlayerInfo)) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to initialize network player %d", playerID);
                return false;                                           // Failed to initialize player
            }
        }
        else {
            // Update existing player data
            m_players[playerID] = receivedPlayerInfo;                   // Update player information
            m_players[playerID].playerID = playerID;                    // Ensure correct player ID
        }

        // Mark as network player
        m_players[playerID].isNetworkPlayer = true;                     // Set network player flag
        m_players[playerID].networkSessionID = m_currentSessionID;      // Set session ID

        // Log network operation for debugging
        LogNetworkOperation("ReceivePlayerInfo", playerID);            // Log operation

        debug.logDebugMessage(LogLevel::LOG_INFO, L"Player %d info successfully received from network (%zu bytes)",
            playerID, packet.data.size());

        return true;                                                    // Player info received successfully
    }
    catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception receiving player %d info: %S", playerID, e.what());
        return false;                                                   // Failed to receive player info
    }
}

// Broadcast player update to all clients
bool GamePlayer::BroadcastPlayerUpdate(int playerID) {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GamePlayer::BroadcastPlayerUpdate() called for player %d", playerID);

    // Validate player
    if (!IsPlayerValid(playerID)) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Cannot broadcast update for invalid player %d", playerID);
        return false;                                                   // Invalid player
    }

    // Check if network functionality is enabled
    if (!m_networkEnabled) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot broadcast player update - network functionality disabled");
        return false;                                                   // Network not enabled
    }

    // Check if network manager is connected
    if (!networkManager.IsConnected()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot broadcast player update - not connected to server");
        return false;                                                   // Not connected to server
    }

    try {
        const PlayerInfo& player = m_players[playerID];                 // Get const reference to player data

        // Serialize player information for network transmission
        std::vector<uint8_t> serializedData = SerializePlayerInfo(player); // Serialize player data

        // Add broadcast header information
        std::vector<uint8_t> broadcastData;                             // Create broadcast data vector
        broadcastData.reserve(serializedData.size() + sizeof(uint32_t)); // Reserve space for header and data

        // Add player ID to broadcast data
        uint32_t networkPlayerID = static_cast<uint32_t>(playerID);     // Convert player ID to network format
        broadcastData.insert(broadcastData.end(),
            reinterpret_cast<uint8_t*>(&networkPlayerID),
            reinterpret_cast<uint8_t*>(&networkPlayerID) + sizeof(networkPlayerID)); // Add player ID

        // Add serialized player data
        broadcastData.insert(broadcastData.end(), serializedData.begin(), serializedData.end()); // Add player data

        // Send broadcast packet via network manager
        if (!networkManager.SendPacket(NetworkCommand::CMD_GAME_UPDATE, broadcastData)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to broadcast player %d update", playerID);
            return false;                                               // Failed to broadcast
        }

        // Log network operation for debugging
        LogNetworkOperation("BroadcastPlayerUpdate", playerID);        // Log operation

        debug.logDebugMessage(LogLevel::LOG_INFO, L"Player %d update successfully broadcast (%zu bytes)",
            playerID, broadcastData.size());

        return true;                                                    // Update broadcast successfully
    }
    catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception broadcasting player %d update: %S", playerID, e.what());
        return false;                                                   // Failed to broadcast update
    }
}

// Handle incoming network player data
bool GamePlayer::HandleNetworkPlayerData(const NetworkPacket& packet) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GamePlayer::HandleNetworkPlayerData() called");

    // Check if network functionality is enabled
    if (!m_networkEnabled) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot handle network player data - network functionality disabled");
        return false;                                                   // Network not enabled
    }

    // Validate packet data size
    if (packet.data.size() < sizeof(uint32_t)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Network packet too small for player data");
        return false;                                                   // Packet too small
    }

    try {
        // Extract player ID from packet data
        uint32_t networkPlayerID;                                       // Network player ID
        memcpy(&networkPlayerID, packet.data.data(), sizeof(networkPlayerID)); // Extract player ID

        // Validate extracted player ID
        int playerID = static_cast<int>(networkPlayerID);               // Convert to local player ID
        if (!ValidatePlayerID(playerID)) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid network player ID %d", playerID);
            return false;                                               // Invalid player ID
        }

        // Extract serialized player data
        std::vector<uint8_t> playerData(packet.data.begin() + sizeof(uint32_t), packet.data.end()); // Extract player data

        // Deserialize player information
        PlayerInfo receivedPlayerInfo;                                  // Create player info structure
        if (!DeserializePlayerInfo(playerData, receivedPlayerInfo)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to deserialize network player data");
            return false;                                               // Failed to deserialize
        }

        // Update or initialize player with received data
        if (!m_playerSlotActive[playerID]) {
            // Initialize new network player
            if (!InitPlayer(playerID, receivedPlayerInfo)) {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to initialize network player %d", playerID);
                return false;                                           // Failed to initialize player
            }
        }
        else {
            // Update existing player data
            m_players[playerID] = receivedPlayerInfo;                   // Update player information
            m_players[playerID].playerID = playerID;                    // Ensure correct player ID
        }

        // Mark as network player and set session information
        m_players[playerID].isNetworkPlayer = true;                     // Set network player flag
        m_players[playerID].networkSessionID = m_currentSessionID;      // Set session ID

        // Log network operation for debugging
        LogNetworkOperation("HandleNetworkPlayerData", playerID);      // Log operation

        debug.logDebugMessage(LogLevel::LOG_INFO, L"Network player %d data successfully handled (%zu bytes)",
            playerID, playerData.size());

        return true;                                                    // Network data handled successfully
    }
    catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception handling network player data: %S", e.what());
        return false;                                                   // Failed to handle network data
    }
}
#endif

// Get number of active players
int GamePlayer::GetActivePlayerCount() const {
    int activeCount = 0;                                                // Initialize active player counter

    // Count active player slots
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (m_playerSlotActive[i]) {
            activeCount++;                                              // Increment counter for active slot
        }
    }

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Active player count: %d", activeCount);
    #endif

    return activeCount;                                                 // Return total active players
}

// Get list of active player IDs
std::vector<int> GamePlayer::GetActivePlayerIDs() const {
    std::vector<int> activePlayerIDs;                                   // Create vector for active player IDs
    activePlayerIDs.reserve(MAX_PLAYERS);                               // Reserve maximum possible space

    // Collect active player IDs
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (m_playerSlotActive[i]) {
            activePlayerIDs.push_back(i);                               // Add active player ID to list
        }
    }

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Retrieved %zu active player IDs", activePlayerIDs.size());
    #endif

    return activePlayerIDs;                                             // Return list of active player IDs
}

// Update all active players with time delta
void GamePlayer::UpdateAllPlayers(float deltaTime) {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GamePlayer::UpdateAllPlayers() called with deltaTime: %.3f", deltaTime);
    #endif

    try {
        // Update each active player
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (m_playerSlotActive[i]) {
                PlayerInfo& player = m_players[i];                      // Get reference to player data

                // Update player timer if active
                if (player.timerActive) {
                    UpdatePlayerTimers(i);                              // Update timing calculations
                }

                // Update player position based on velocity
                player.position2D.x += player.velocity2D.x * deltaTime; // Update 2D X position
                player.position2D.y += player.velocity2D.y * deltaTime; // Update 2D Y position
                player.position3D.x += player.velocity3D.x * deltaTime; // Update 3D X position
                player.position3D.y += player.velocity3D.y * deltaTime; // Update 3D Y position
                player.position3D.z += player.velocity3D.z * deltaTime; // Update 3D Z position

                // TODO: Add additional player update logic here
                // This could include animation updates, state machine processing,
                // physics calculations, etc.
            }
        }

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"All active players updated successfully");
        #endif
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception updating all players: %S", e.what());
        #endif
    }
}

// Get combined score of all players
uint64_t GamePlayer::GetCombinedScore() const {
    uint64_t combinedScore = 0;                                         // Initialize combined score

    // Sum scores of all active players
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (m_playerSlotActive[i]) {
            combinedScore += m_players[i].score;                        // Add player score to total
        }
    }

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Combined player score: %llu", combinedScore);
    #endif

    return combinedScore;                                               // Return total combined score
}

// Get player with highest score
PlayerInfo* GamePlayer::GetHighestScoringPlayer() {
    PlayerInfo* highestPlayer = nullptr;                                // Initialize highest scoring player pointer
    uint64_t highestScore = 0;                                          // Initialize highest score tracker

    // Find player with highest score
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        if (m_playerSlotActive[i] && m_players[i].score > highestScore) {
            highestScore = m_players[i].score;                          // Update highest score
            highestPlayer = &m_players[i];                              // Update highest scoring player
        }
    }

    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        if (highestPlayer) {
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Highest scoring player: %d with score %llu",
                highestPlayer->playerID, highestScore);
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"No active players found for highest score check");
        }
    #endif

    return highestPlayer;                                               // Return pointer to highest scoring player
}

// Reset all player statistics
void GamePlayer::ResetAllPlayerStats() {
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"GamePlayer::ResetAllPlayerStats() called - resetting all player statistics");
    #endif

    try {
        // Reset statistics for all active players
        for (int i = 0; i < MAX_PLAYERS; ++i) {
            if (m_playerSlotActive[i]) {
                PlayerInfo& player = m_players[i];                      // Get reference to player data

                // Reset scoring and progress statistics
                player.score = 0;                                       // Reset current score
                player.experience = 0;                                  // Reset experience points
                player.level = 1;                                       // Reset to level 1
                player.experienceToNext = 1000;                        // Reset experience to next level

                // Reset health and combat statistics
                player.health = player.maxHealth;                      // Restore to maximum health
                player.armour = 0;                                      // Clear armour
                player.shield = 0;                                      // Clear shield
                player.mana = player.maxMana;                          // Restore to maximum mana
                player.energy = player.maxEnergy;                      // Restore to maximum energy
                player.ammunition = player.maxAmmunition;               // Restore to maximum ammunition

                // Reset player state
                player.isDead = false;                                  // Mark as alive
                player.deathAnimation = DeathAnimationState::NONE;      // Clear death animation
                player.currentState = PlayerState::ACTIVE;             // Set to active state

                // Reset timer information
                if (player.timerActive) {
                    StopPlayerTimer(i);                                 // Stop active timer
                }

                // Clear achievement and progress data
                player.unlockedAchievements.clear();                   // Clear achievements
                player.completedQuests.clear();                        // Clear completed quests
                player.discoveredAreas.clear();                        // Clear discovered areas

                #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Statistics reset for player %d", i);
                #endif
            }
        }

        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"All player statistics successfully reset");
        #endif
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception resetting player statistics: %S", e.what());
        #endif
    }
}

//==============================================================================
// Private Helper Functions Implementation
//==============================================================================

// Validate player ID range
bool GamePlayer::ValidatePlayerID(int playerID) const {
    return (playerID >= 0 && playerID < MAX_PLAYERS);                   // Check if ID is within valid range
}

// Reset player information to defaults
void GamePlayer::ResetPlayerInfo(int playerID) {
    // Validate player ID
    if (!ValidatePlayerID(playerID)) {
        return;                                                         // Invalid player ID
    }

    // Reset player information to default constructor values
    m_players[playerID] = PlayerInfo();                                 // Use default constructor
    m_players[playerID].playerID = playerID;                            // Set correct player ID

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %d information reset to defaults", playerID);
#endif
}

// Load binary file into vector
bool GamePlayer::LoadBinaryFile(const std::string& filename, std::vector<uint8_t>& data) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Loading binary file: %S", filename.c_str());
#endif

    try {
        // Open file in binary mode
        std::ifstream file(filename, std::ios::binary | std::ios::ate); // Open at end to get size

        // Check if file opened successfully
        if (!file.is_open()) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to open file: %S", filename.c_str());
#endif
            return false;                                               // Failed to open file
        }

        // Get file size
        std::streamsize fileSize = file.tellg();                       // Get current position (file size)
        file.seekg(0, std::ios::beg);                                   // Seek back to beginning

        // Validate file size
        if (fileSize <= 0) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid file size: %lld bytes", static_cast<long long>(fileSize));
#endif
            return false;                                               // Invalid file size
        }

        // Resize vector and read file data
        data.resize(static_cast<size_t>(fileSize));                     // Resize vector to file size
        file.read(reinterpret_cast<char*>(data.data()), fileSize);      // Read file data into vector

        // Check if read was successful
        if (!file.good()) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to read file data: %S", filename.c_str());
#endif
            data.clear();                                               // Clear data on failure
            return false;                                               // Failed to read file
        }

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Binary file successfully loaded: %lld bytes",
            static_cast<long long>(fileSize));
#endif

        return true;                                                    // File loaded successfully
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception loading binary file: %S", e.what());
#endif
        data.clear();                                                   // Clear data on exception
        return false;                                                   // Failed to load file
    }
}

// Update player timing calculations
void GamePlayer::UpdatePlayerTimers(int playerID) {
    // Validate player ID
    if (!ValidatePlayerID(playerID)) {
        return;                                                         // Invalid player ID
    }

    PlayerInfo& player = m_players[playerID];                           // Get reference to player data

    // Update timer calculations if timer is active
    if (player.timerActive) {
        player.timerCurrent = std::chrono::steady_clock::now();         // Update current time
        player.totalTimeElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            player.timerCurrent - player.timerStart);                  // Calculate total elapsed time

#if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player %d timer updated: %lld ms elapsed",
            playerID, player.totalTimeElapsed.count());
#endif
    }
}

// Get bitmap array index from coordinates
size_t GamePlayer::GetBitmapIndex(int playerID, const Vector2& point) const {
    // Validate player ID
    if (!ValidatePlayerID(playerID)) {
        return 0;                                                       // Return safe index for invalid player
    }

    const PlayerInfo& player = m_players[playerID];                     // Get const reference to player data

    // Calculate bitmap index from 2D coordinates
    int x = static_cast<int>(point.x);                                  // Convert X coordinate to integer
    int y = static_cast<int>(point.y);                                  // Convert Y coordinate to integer

    // Calculate array index using row-major order
    size_t index = static_cast<size_t>(y) * static_cast<size_t>(player.bitmapWidth) + static_cast<size_t>(x);

    return index;                                                       // Return calculated index
}

// Validate bitmap coordinates
bool GamePlayer::IsValidBitmapCoordinate(int playerID, const Vector2& point) const {
    // Validate player ID
    if (!ValidatePlayerID(playerID)) {
        return false;                                                   // Invalid player ID
    }

    const PlayerInfo& player = m_players[playerID];                     // Get const reference to player data

    // Check if coordinates are within bitmap bounds
    int x = static_cast<int>(point.x);                                  // Convert X coordinate to integer
    int y = static_cast<int>(point.y);                                  // Convert Y coordinate to integer

    return (x >= 0 && x < player.bitmapWidth &&                        // Check X coordinate bounds
        y >= 0 && y < player.bitmapHeight);                        // Check Y coordinate bounds
}

#if defined(_USE_NETWORKMANAGER_)
// Serialize player data for network transmission
std::vector<uint8_t> GamePlayer::SerializePlayerInfo(const PlayerInfo& playerInfo) {
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Serializing player info for network transmission");

    std::vector<uint8_t> serializedData;                               // Create serialized data vector
    serializedData.reserve(512);                                       // Reserve space for common player data size

    try {
        // Serialize basic player identification
        uint32_t playerID = static_cast<uint32_t>(playerInfo.playerID); // Convert player ID to network format
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&playerID),
            reinterpret_cast<const uint8_t*>(&playerID) + sizeof(playerID)); // Add player ID

        // Serialize player name length and data
        uint32_t nameLength = static_cast<uint32_t>(playerInfo.playerName.length()); // Get name length
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&nameLength),
            reinterpret_cast<const uint8_t*>(&nameLength) + sizeof(nameLength)); // Add name length
        serializedData.insert(serializedData.end(),
            playerInfo.playerName.begin(), playerInfo.playerName.end()); // Add name data

        // Serialize position data
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&playerInfo.position2D),
            reinterpret_cast<const uint8_t*>(&playerInfo.position2D) + sizeof(Vector2)); // Add 2D position
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&playerInfo.position3D),
            reinterpret_cast<const uint8_t*>(&playerInfo.position3D) + sizeof(Vector3)); // Add 3D position

        // Serialize velocity data
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&playerInfo.velocity2D),
            reinterpret_cast<const uint8_t*>(&playerInfo.velocity2D) + sizeof(Vector2)); // Add 2D velocity
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&playerInfo.velocity3D),
            reinterpret_cast<const uint8_t*>(&playerInfo.velocity3D) + sizeof(Vector3)); // Add 3D velocity

        // Serialize player state and status
        uint8_t state = static_cast<uint8_t>(playerInfo.currentState);  // Convert state to byte
        serializedData.push_back(state);                               // Add player state
        serializedData.push_back(playerInfo.isDead ? 1 : 0);           // Add death status
        serializedData.push_back(playerInfo.isActive ? 1 : 0);         // Add active status

        // Serialize health and combat data
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&playerInfo.health),
            reinterpret_cast<const uint8_t*>(&playerInfo.health) + sizeof(int)); // Add health
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&playerInfo.armour),
            reinterpret_cast<const uint8_t*>(&playerInfo.armour) + sizeof(int)); // Add armour
        serializedData.insert(serializedData.end(),
            reinterpret_cast<const uint8_t*>(&playerInfo.score),
            reinterpret_cast<const uint8_t*>(&playerInfo.score) + sizeof(uint64_t)); // Add score

        // Use PUNPack to compress serialized data for network efficiency
        if (punPack.IsInitialized()) {
            PackResult packResult = punPack.PackBuffer(serializedData, CompressionType::LZ77, true);
            if (packResult.compressedSize > 0 && packResult.compressionRatio > 1.0f) {
                // Use compressed data if compression was beneficial
                serializedData = packResult.compressedData;             // Replace with compressed data

                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player info compressed: %zu -> %zu bytes (ratio: %.2f)",
                    packResult.originalSize, packResult.compressedSize, packResult.compressionRatio);
            }
        }

        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player info serialized successfully (%zu bytes)", serializedData.size());

        return serializedData;                                          // Return serialized player data
    }
    catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception serializing player info: %S", e.what());
        return std::vector<uint8_t>();                                  // Return empty vector on error
    }
}

// Deserialize network player data
bool GamePlayer::DeserializePlayerInfo(const std::vector<uint8_t>& data, PlayerInfo& playerInfo) {
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Deserializing player info from network data (%zu bytes)", data.size());

    // Validate input data size
    if (data.empty()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Cannot deserialize empty player data");
        return false;                                                   // Cannot deserialize empty data
    }

    try {
        std::vector<uint8_t> workingData = data;                        // Create working copy of data

        // Try to decompress data using PUNPack if available
        if (punPack.IsInitialized()) {
            // Attempt to create PackResult from received data
            PackResult packResult;                                      // Create pack result structure
            packResult.compressedData = data;                          // Set compressed data
            packResult.compressedSize = data.size();                   // Set compressed size
            packResult.isEncrypted = true;                             // Assume data is encrypted
            // Note: In a real implementation, you would need to properly reconstruct
            // the PackResult structure from the network data

            UnpackResult unpackResult = punPack.UnpackBuffer(packResult); // Attempt decompression
            if (unpackResult.success) {
                workingData = unpackResult.data;                       // Use decompressed data

                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player data decompressed: %zu -> %zu bytes",
                    data.size(), workingData.size());
            }
        }

        size_t offset = 0;                                              // Initialize data offset counter

        // Deserialize basic player identification
        if (offset + sizeof(uint32_t) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for player ID");
            return false;                                               // Insufficient data
        }

        uint32_t networkPlayerID;                                       // Network format player ID
        memcpy(&networkPlayerID, workingData.data() + offset, sizeof(networkPlayerID)); // Extract player ID
        playerInfo.playerID = static_cast<int>(networkPlayerID);        // Convert to local format
        offset += sizeof(uint32_t);                                    // Advance offset

        // Deserialize player name length and data
        if (offset + sizeof(uint32_t) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for player name length");
            return false;                                               // Insufficient data
        }

        uint32_t nameLength;                                            // Player name length
        memcpy(&nameLength, workingData.data() + offset, sizeof(nameLength)); // Extract name length
        offset += sizeof(uint32_t);                                    // Advance offset

        // Validate name length
        if (nameLength > 256 || offset + nameLength > workingData.size()) { // Reasonable name length limit
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid player name length: %u", nameLength);
            return false;                                               // Invalid name length
        }

        // Extract player name
        playerInfo.playerName.assign(workingData.begin() + offset,
            workingData.begin() + offset + nameLength);                // Extract player name
        offset += nameLength;                                           // Advance offset

        // Deserialize position data
        if (offset + sizeof(Vector2) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for 2D position");
            return false;                                               // Insufficient data
        }

        memcpy(&playerInfo.position2D, workingData.data() + offset, sizeof(Vector2)); // Extract 2D position
        offset += sizeof(Vector2);                                     // Advance offset

        if (offset + sizeof(Vector3) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for 3D position");
            return false;                                               // Insufficient data
        }

        memcpy(&playerInfo.position3D, workingData.data() + offset, sizeof(Vector3)); // Extract 3D position
        offset += sizeof(Vector3);                                     // Advance offset

        // Deserialize velocity data
        if (offset + sizeof(Vector2) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for 2D velocity");
            return false;                                               // Insufficient data
        }

        memcpy(&playerInfo.velocity2D, workingData.data() + offset, sizeof(Vector2)); // Extract 2D velocity
        offset += sizeof(Vector2);                                     // Advance offset

        if (offset + sizeof(Vector3) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for 3D velocity");
            return false;                                               // Insufficient data
        }

        memcpy(&playerInfo.velocity3D, workingData.data() + offset, sizeof(Vector3)); // Extract 3D velocity
        offset += sizeof(Vector3);                                     // Advance offset

        // Deserialize player state and status
        if (offset + 3 > workingData.size()) {                         // Need 3 bytes for state flags
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for player state");
            return false;                                               // Insufficient data
        }

        uint8_t state = workingData[offset++];                          // Extract player state
        playerInfo.currentState = static_cast<PlayerState>(state);     // Convert to player state enum
        playerInfo.isDead = (workingData[offset++] != 0);              // Extract death status
        playerInfo.isActive = (workingData[offset++] != 0);            // Extract active status

        // Deserialize health and combat data
        if (offset + sizeof(int) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for health");
            return false;                                               // Insufficient data
        }

        memcpy(&playerInfo.health, workingData.data() + offset, sizeof(int)); // Extract health
        offset += sizeof(int);                                         // Advance offset

        if (offset + sizeof(int) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for armour");
            return false;                                               // Insufficient data
        }

        memcpy(&playerInfo.armour, workingData.data() + offset, sizeof(int)); // Extract armour
        offset += sizeof(int);                                         // Advance offset

        if (offset + sizeof(uint64_t) > workingData.size()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Insufficient data for score");
            return false;                                               // Insufficient data
        }

        memcpy(&playerInfo.score, workingData.data() + offset, sizeof(uint64_t)); // Extract score
        offset += sizeof(uint64_t);                                    // Advance offset

        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Player info deserialized successfully (player %d: %S)",
            playerInfo.playerID, std::wstring(playerInfo.playerName.begin(), playerInfo.playerName.end()).c_str());

        return true;                                                    // Deserialization successful
    }
    catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"Exception deserializing player info: %S", e.what());
        return false;                                                   // Failed to deserialize
    }
}

// Log network operations for debugging
void GamePlayer::LogNetworkOperation(const std::string& operation, int playerID) {
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Network operation: %S for player %d (Session: %S)",
        operation.c_str(), playerID, m_currentSessionID.c_str());
}
#endif

