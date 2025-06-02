#include "Includes.h"
#include "Configuration.h"
#include "Debug.h"
#include "WinSystem.h"

extern Debug debug;
extern SystemUtils sysUtils;

// Constructor
Configuration::Configuration() : configFile(L"GameConfig.cfg") {
    loadConfig();
}

// Destructor
Configuration::~Configuration() {
	if (bShutdownComplete) return;                                      // Prevent double destruction
    // Optionally save the configuration before destruction
    saveConfig();
    bShutdownComplete = true;
}

// Instance method to get the current configuration
MyConfig Configuration::GetConfig() const {
    return myConfig;
}

// Instance method to load the configuration from a file
bool Configuration::loadConfig() {
    std::ifstream configStream(configFile);
    if (!configStream.is_open()) {
        std::wstring msg = L"Error opening config file: " + configFile;
        debug.logLevelMessage(LogLevel::LOG_ERROR, msg);
        return false;
    }

    // Deserialize the configuration from JSON
    try {
        json j;
        configStream >> j;
        myConfig.current_money = j["current_money"];
        myConfig.level = j["level"];
        myConfig.playMusic = j["playMusic"];
        myConfig.musicVolume = j["musicVolume"];
        myConfig.masterVolume = j["masterVolume"];
        myConfig.ambientVolume = j["ambientVolume"];
        myConfig.dialogVolume = j["dialogVolume"];
        myConfig.buildVersion = j["buildVersion"];
        myConfig.enableVSync = j["enableVSync"];
        myConfig.msaaEnabled = j["msaaEnabled"];
        myConfig.antiAliasingEnabled = j["antiAliasingEnabled"];
        myConfig.MipMapping = j["MipMapping"];
        myConfig.BackCulling = j["BackCulling"];
        myConfig.buildVersion = j["buildVersion"];
        myConfig.buildSubVersion = j["buildSubVersion"];
        myConfig.build = j["build"];
        myConfig.fov = j["fov"];
        myConfig.maxPitch = j["maxPitch"];
        myConfig.minPitch = j["minPitch"];
        myConfig.nearPlane = j["nearPlane"];
        myConfig.farPlane = j["farPlane"];
        myConfig.aspectRatio = j.value("aspectRatio", 16.0 / 9.0);
        myConfig.zoomSensitivity = j["zoomSensitivity"];
        myConfig.moveSensitivity = j["moveSensitivity"];
        myConfig.TTSVolume = j["TTSVolume"];
        myConfig.UseTTS = j["UseTTS"];
        myConfig.chksum = j["chksum"];
    }
    catch (const std::exception& e) {
        std::wstring msg = L"Error loading configuration: " + sysUtils.widen(e.what());
        debug.logLevelMessage(LogLevel::LOG_ERROR, msg);
        return false;
    }

    // Validate checksum, if fails, reset to defaults and force save
    if (!validateChecksum(myConfig)) 
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Checksum validation failed / Tamper Proof Protection detected - RESETTING!!!!");

        // Set to default base configuration
        myConfig = MyConfig();  // Default initialization
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Adopting default base configuration due to checksum failure.");

        // Save default config immediately
        if (!saveConfig()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to save default configuration after checksum failure!");
        }
        else {
            #if defined(_DEBUG_CONFIGURATION_)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Default configuration saved successfully.");
            #endif
        }
    }

    #if defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Configuration file loaded successfully.");
    #endif
    return true;
}

// Instance method to save the current configuration to a file
bool Configuration::saveConfig() {
    std::ofstream configStream(configFile);
    if (!configStream.is_open()) {
        std::wstring msg = L"Error opening config file for saving: " + configFile;
        debug.logLevelMessage(LogLevel::LOG_ERROR, msg);
        return false;
    }

    // Serialize the configuration to JSON
    try {
        json j;
        j["buildVersion"] = myConfig.buildVersion;
        j["buildSubVersion"] = myConfig.buildSubVersion;
        j["build"] = myConfig.build;
        j["current_money"] = myConfig.current_money;
        j["level"] = myConfig.level;
        j["playMusic"] = myConfig.playMusic;
        j["musicVolume"] = myConfig.musicVolume;
        j["masterVolume"] = myConfig.masterVolume;
        j["ambientVolume"] = myConfig.ambientVolume;
        j["dialogVolume"] = myConfig.dialogVolume;
        j["enableVSync"] = myConfig.enableVSync;
        j["msaaEnabled"] = myConfig.msaaEnabled;
        j["antiAliasingEnabled"] = myConfig.antiAliasingEnabled;
        j["MipMapping"] = myConfig.MipMapping;
        j["BackCulling"] = myConfig.BackCulling;
        j["fov"] = myConfig.fov;
        j["maxPitch"] = myConfig.maxPitch;
        j["minPitch"] = myConfig.minPitch;
        j["zoomSensitivity"] = myConfig.zoomSensitivity;
        j["moveSensitivity"] = myConfig.moveSensitivity;
        j["nearPlane"] = myConfig.nearPlane;
		j["farPlane"] = myConfig.farPlane;
        j["aspectRatio"] = myConfig.aspectRatio;
        j["UseTTS"] = myConfig.UseTTS;
        j["TTSVolume"] = myConfig.TTSVolume;
        j["chksum"] = calculateChecksum(myConfig);

        configStream << j.dump(4);  // Pretty print the JSON with 4 spaces
    }
    catch (const std::exception& e) {
		std::wstring msg = L"Error saving configuration: " + sysUtils.widen(e.what());
        debug.logLevelMessage(LogLevel::LOG_ERROR, msg);        
        return false;
    }

    return true;
}

// Instance method to update the configuration
void Configuration::updateConfig(const MyConfig& newConfig) {
    myConfig = newConfig;  // Update the configuration
}

// Calculate checksum for the configuration
long double Configuration::calculateChecksum(const MyConfig& cfg) const
{
    // Create a single string that contains the important values, 
    // that is everything, but the cksum field.
    std::ostringstream ss;
    ss << static_cast<int>(cfg.playMusic)
        << static_cast<int>(cfg.enableVSync)
        << static_cast<int>(cfg.msaaEnabled)
        << static_cast<int>(cfg.antiAliasingEnabled)
        << static_cast<int>(cfg.MipMapping)
        << static_cast<int>(cfg.BackCulling)
        << static_cast<int>(cfg.UseTTS)
        << cfg.musicVolume
        << cfg.masterVolume
        << cfg.ambientVolume
        << cfg.dialogVolume
        << cfg.buildVersion
		<< cfg.buildSubVersion
		<< cfg.build
		<< cfg.fov
        << cfg.maxPitch
        << cfg.minPitch
        << cfg.zoomSensitivity
        << cfg.moveSensitivity
        << cfg.nearPlane
		<< cfg.farPlane
        << cfg.aspectRatio
        << cfg.level
        << cfg.TTSVolume
        << std::fixed << std::setprecision(4) << cfg.current_money;

    std::string combined = ss.str();
    // FNV-1a 64-bit hash implementation
    uint64_t hash = 14695981039346656037ULL;
    for (char c : combined) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }

    // Convert to long double in a way that's not obviously tied to input
    long double obscured = static_cast<long double>(hash) / 1337.77;

    return obscured;
}

// Validate checksum
bool Configuration::validateChecksum(const MyConfig& cfg) const
{
    long double calculatedChecksum = calculateChecksum(cfg);

    // Use tighter and cleaner comparison logic
    constexpr long double epsilon = 0.00001;

    if (std::fabs(calculatedChecksum - cfg.chksum) < epsilon) {
        return true;
    }

    // Optional: logging/debugging here
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Configuration checksum mismatch detected.");

    return false;
}

// Getters and setters for the configFile
std::wstring Configuration::getConfigFile() const {
    return configFile;
}

void Configuration::setConfigFile(const std::wstring& filename) {
    configFile = filename;
}
