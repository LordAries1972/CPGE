#pragma once

#include "Debug.h"

#include <nlohmann/json.hpp>

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
    int buildVersion = 1;
    int buildSubVersion = 0;
    int build = 1;

    bool playMusic = true;
    bool enableVSync = true;
    bool msaaEnabled = false;
    bool antiAliasingEnabled = true;
    bool MipMapping = true;
    bool BackCulling = true;

    long double fov = 60.0f;
    long double zoomSensitivity = 0.05f;
    long double moveSensitivity = 0.005f;
    long double nearPlane = 0.1f;
    long double farPlane = 1000.0f;
    long double aspectRatio = 16.0 / 9.0;                                       // Default widescreen
    long double maxPitch = 89.0f;                                               // Degrees
    long double minPitch = -89.0f;                                              // Degrees

    // Add more configuration parameters as needed
    bool UseTTS = true;
    double TTSVolume = 1.0f;
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

    // You can still keep these if needed
    std::wstring getConfigFile() const;
    void setConfigFile(const std::wstring& filename);

private:
    std::wstring configFile;
    long double calculateChecksum(const MyConfig& cfg) const;
    bool validateChecksum(const MyConfig& cfg) const;

    bool bShutdownComplete = false;

	// Disable copy constructor and assignment operator
    Configuration(const Configuration&) = delete;
    Configuration& operator=(const Configuration&) = delete;
};

// This must remain here after the fact of declaration
// DO NOT PLACE above class declaration.
extern Configuration config;
extern Debug debug;

extern const std::string lpCONFIG_FILENAME;
