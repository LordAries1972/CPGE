// STEAM.h
#pragma once
#include "Debug.h"
#include "Constants.h"

#include <steam/steam_api.h>
#include <string>
#include <vector>

class STEAM {
public:
    STEAM();
    ~STEAM();

    bool Initialize();
    void Shutdown();
    void RunCallbacks();

    std::string GetPlayerName();
    uint64_t GetPlayerSteamID();

    bool UnlockAchievement(const std::string& achievementID);
    bool ResetAchievement(const std::string& achievementID);
    bool IsAchievementUnlocked(const std::string& achievementID);

    bool SetStat(const std::string& statID, int value);
    int GetStat(const std::string& statID);

    std::vector<std::string> GetFriendList();

private:
    bool m_bInitialized;
};

extern Debug debug;
