#pragma once

#include "BuildInfo.h"
#include "Debug.h"

#include <nlohmann/json.hpp>
#include <functional>

/* -------------------------------------------------------- */
// Music Player Configuration
/* -------------------------------------------------------- */
// Ensure only ONE Music player type is defined only
#if (defined(__USE_XMPLAYER__) + defined(__USE_MP3PLAYER__)) > 1 
#error "Multiple Music Players are Defined for use. Please define only one if you are wanting Music Playback."
#endif

using json = nlohmann::json;

extern const std::string lpCONFIG_FILENAME;

struct MyConfig {
    long double chksum = 0.0;
    long int current_money = 0;
    long int level = 1;

    int musicVolume = 64;
    int masterVolume = 64;
    int ambientVolume = 64;
    int dialogVolume = 64;
    int buildVersion    = CURRENT_BUILD_VERSION;
    int buildSubVersion = CURRENT_BUILD_SUBVERSION;
    int build           = CURRENT_BUILD;

    bool playMusic = true;
    bool enableVSync = true;
    bool msaaEnabled = false;
    bool antiAliasingEnabled = true;
    bool MipMapping = true;
    bool BackCulling = true;
    bool showDebugInfo = false;

    long double fov = 60.0f;
    long double zoomSensitivity = 0.005f;
    long double moveSensitivity = 0.0005f;
    long double joystickSensitivity = 0.01f;
    long double joystickRotationSensitivity = 0.001f;
    long double nearPlane = 0.1f;
    long double farPlane = 1000.0f;
    long double aspectRatio = 16.0 / 9.0;                                       // Default widescreen
    long double maxPitch = 89.0f;                                               // Degrees
    long double minPitch = -89.0f;                                              // Degrees

    long double microphoneVolume = 2.5f;

    // Add more configuration parameters as needed
    bool UseTTS = true;
    long double TTSVolume = 1.0f;

    // Display / window settings.
    // Defaults match DEFAULT_WINDOW_WIDTH / DEFAULT_WINDOW_HEIGHT (800×600, windowed).
    // loadConfig() overwrites these with the saved values on success.
    // If GameConfig.cfg is absent or corrupt, 800×600 windowed is the safe fallback.
    int displayMode      = 0;   // 0=Windowed  1=Borderless  2=Full Screen
    int resolutionWidth  = 800;
    int resolutionHeight = 600;
    int refreshRate      = 60;

    // Renderer selection — clamped to the valid range for the current platform by
    // Configuration::ValidateRendererForPlatform() at load time and on every save.
    // Windows:       0=DirectX 11 (default)  1=DirectX 12  2=OpenGL  3=Vulkan
    // Linux/Android: 0=OpenGL (default)       1=Vulkan
    // iOS/macOS:     0=OpenGL (only option)
    int rendererType     = 0;

    // Swap-chain buffer mode: 1 = triple buffering (default), 0 = double buffering.
    // Takes effect after a video-settings restart.
    int buffering        = 1;

#ifdef PROJECT_ONLY_CODE
    // --- Player profile fields (TSOO project specific) ---
    // profileID   : index into kCommanderRoster (0..13)
    // playerName  : player-chosen callsign (max 24 chars, no special characters)
    // playerExperience : accumulated XP across all sessions
    // These three fields are included in the checksum so manual edits
    // trigger an automatic reset of ALL config values to defaults.
    int         profileID        = 0;
    std::string playerName       = "Commander";
    uint64_t    playerExperience = 0;
#endif
};

class Configuration {
public:
    MyConfig myConfig;

    Configuration();      // No filename argument anymore
    ~Configuration();

    MyConfig GetConfig() const;
    bool loadConfig();
    bool saveConfig();
    void updateConfig(const MyConfig& newConfig);
    void setOnApplyCallback(std::function<void(const MyConfig&)> cb);
    void applyLive() const;

    // Clamps rendererType to the valid range for the compiled platform.
    static int ValidateRendererForPlatform(int type);

    // You can still keep these if needed
    std::wstring getConfigFile() const;
    void setConfigFile(const std::wstring& filename);

private:
    std::wstring configFile;
    long double calculateChecksum(const MyConfig& cfg) const;
    bool validateChecksum(const MyConfig& cfg) const;

    bool bShutdownComplete = false;
    std::function<void(const MyConfig&)> onApply;

	// Disable copy constructor and assignment operator
    Configuration(const Configuration&) = delete;
    Configuration& operator=(const Configuration&) = delete;
};

// This must remain here after the fact of declaration
// DO NOT PLACE above class declaration.
extern Configuration config;
extern Debug debug;

extern const std::string lpCONFIG_FILENAME;
