// STEAM.cpp
#include <nlohmann/json.hpp>
#include "MySteam.h"
#include <iostream>

STEAM::STEAM() : m_bInitialized(false) {}

STEAM::~STEAM() {
    Shutdown();
    debug.logLevelMessage(Constants::LogLevel::LOG_INFO, L"[STEAM] has been destroyed.");
}

bool STEAM::Initialize() {
    if (SteamAPI_RestartAppIfNecessary(k_uAppIdInvalid)) {
		debug.logLevelMessage(Constants::LogLevel::LOG_ERROR, L"[STEAM] Application not running under Steam.");
        return false;
    }
    if (!SteamAPI_Init()) {
		debug.logLevelMessage(Constants::LogLevel::LOG_ERROR, L"[STEAM] Steam API initialization failed.");
        return false;
    }
    m_bInitialized = true;
    return true;
}

void STEAM::Shutdown() {
    if (m_bInitialized) {
        SteamAPI_Shutdown();
        m_bInitialized = false;
    }
}

void STEAM::RunCallbacks() {
    if (m_bInitialized) {
        SteamAPI_RunCallbacks();
    }
}

std::string STEAM::GetPlayerName() {
    if (!m_bInitialized) return "";
    return SteamFriends()->GetPersonaName();
}

uint64_t STEAM::GetPlayerSteamID() {
    if (!m_bInitialized) return 0;
    return SteamUser()->GetSteamID().ConvertToUint64();
}

bool STEAM::UnlockAchievement(const std::string& achievementID) {
    if (!m_bInitialized) return false;
    SteamUserStats()->SetAchievement(achievementID.c_str());
    return SteamUserStats()->StoreStats();
}

bool STEAM::ResetAchievement(const std::string& achievementID) {
    if (!m_bInitialized) return false;
    SteamUserStats()->ClearAchievement(achievementID.c_str());
    return SteamUserStats()->StoreStats();
}

bool STEAM::IsAchievementUnlocked(const std::string& achievementID) {
    if (!m_bInitialized) return false;
    bool achieved = false;
    SteamUserStats()->GetAchievement(achievementID.c_str(), &achieved);
    return achieved;
}

bool STEAM::SetStat(const std::string& statID, int value) {
    if (!m_bInitialized) return false;
    SteamUserStats()->SetStat(statID.c_str(), value);
    return SteamUserStats()->StoreStats();
}

int STEAM::GetStat(const std::string& statID) {
    if (!m_bInitialized) return 0;
    int value = 0;
    SteamUserStats()->GetStat(statID.c_str(), &value);
    return value;
}

std::vector<std::string> STEAM::GetFriendList() {
    std::vector<std::string> friends;
    if (!m_bInitialized) return friends;

    int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
    for (int i = 0; i < friendCount; ++i) {
        CSteamID friendID = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);
        friends.push_back(SteamFriends()->GetFriendPersonaName(friendID));
    }
    return friends;
}
