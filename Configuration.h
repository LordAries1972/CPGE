#pragma once

#include "BuildInfo.h"
#include "Debug.h"

#include <nlohmann/json.hpp>
#include <functional>

using json = nlohmann::json;

extern const std::string lpCONFIG_FILENAME;

struct MyConfig {
    long double chksum = 0.0;
    long int current_money = 0;
    long int level = 1;

    int musicVolume = 10;
    int masterVolume = 64;
    int ambientVolume = 48;
    int dialogVolume = 51;
    int buildVersion    = CURRENT_BUILD_VERSION;
    int buildSubVersion = CURRENT_BUILD_SUBVERSION;
    int build           = CURRENT_BUILD;

    bool playMusic = true;
    bool enableVSync = true;
    bool msaaEnabled = false;
    bool antiAliasingEnabled = true;
    int  msaaSamples = 4;               // 2 / 4 / 8 - requested MSAA sample count (only used while msaaEnabled && antiAliasingEnabled).
                                        // Loaded with j.value() and NOT part of calculateChecksum() so older configs keep validating.
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

    long double microphoneVolume = 0.8f;    // Mic gain 0.0 - 1.0

    // Add more configuration parameters as needed
    bool UseTTS = true;
    long double TTSVolume = 1.0f;

    // Display / window settings.
    // Defaults to 800×600 windowed; loadConfig() overwrites these with saved values.
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

    // Shadow mapping (Video tab, below Mip Mapping).  Loaded with j.value() defaults so
    // older GameConfig.cfg files keep loading; NOT part of calculateChecksum() because
    // adding fields there would fail validation on every existing config and reset it.
    //   shadowsEnabled  : master switch (applies live)
    //   shadowQuality   : 0=Low (1024 dir / 512 local)  1=Medium (2048 / 1024)
    //                     2=High (4096 / 1024) - shadow map resolution, needs restart
    //   maxSpotShadows  : spot lights that cast shadows per frame   (0 - 8, live)
    //   maxPointShadows : point lights that cast shadows per frame  (0 - 4, live)
    //   shadowDistance  : directional shadow radius around the camera in world units (live)
    bool        shadowsEnabled   = true;
    int         shadowQuality    = 1;
    int         maxSpotShadows   = 8;
    int         maxPointShadows  = 4;
    long double shadowDistance   = 200.0;

    // Scene reflections (Video tab, below the shadow options).  Same persistence rules as the
    // shadow settings (j.value defaults, not checksummed).  See "Scene Reflections" in Lights.h.
    //   reflectionsEnabled : master switch (live)
    //   reflectionQuality  : 0=Low (64px cube)  1=Medium (128px)  2=High (256px) - needs restart
    //   reflectionStrength : 0.0 - 2.0 multiplier on the scene reflection (live)
    //   reflectionBlur     : 0.0 - 3.0 extra mip bias that softens reflections (live)
    //   reflectionUpdate   : 0=Slow  1=Normal  2=Every frame - how fast the probe follows lighting (live)
    bool        reflectionsEnabled = true;
    int         reflectionQuality  = 1;
    long double reflectionStrength = 1.0;
    long double reflectionBlur     = 0.0;
    int         reflectionUpdate   = 1;
    bool        reflectionLive     = true;               // Capture the real scene into the reflection cube (otherwise sky only)

    // Planar reflections (Video tab, below the reflection options).  Same persistence rules.
    // See "Planar Reflections" in Lights.h.
    //   planarEnabled    : master switch (live)
    //   planarQuality    : 0=640x360  1=960x540  2=1280x720 mirror render target - needs restart
    //   planarStrength   : 0.0 - 1.0 global multiplier (live)
    //   planarDistortion : 0.0 - 1.0 ripple from the surface normal (live)
    //   planarUpdate     : 0=every frame  1=every 2nd  2=every 3rd frame (live)
    bool        planarEnabled    = true;
    int         planarQuality    = 1;
    long double planarStrength   = 1.0;
    long double planarDistortion = 0.3;
    int         planarUpdate     = 0;
    int         planarMaxPlanes  = 2;                    // 1 - 4 distinct reflection planes rendered per frame (live)

    // Display image adjustment (Video tab).  1.0 = neutral for both.
    //   brightness : 0.5 - 1.5
    //   contrast   : 0.5 - 1.5
    // Same persistence rules as the shadow settings above (j.value defaults, not checksummed).
    long double brightness       = 1.0;
    long double contrast         = 1.0;

    // Emission (Video tab).  Applied by every renderer when it uploads the material's emissive strength:
    //   final strength = material emissiveStrength (as authored) * EmissionScale()
    //   emissionEnabled   : false = emissive glow off (scale 0)
    //   emissionIntensity : 0.0 - 3.0, 1.0 = the model's authored strength (live)
    //   emissionPulse     : RUNTIME ONLY (never saved).  0.0 - 1.0 multiplier driven by the
    //                       FXManager EmissionPulsator effect; 1.0 = no pulse.  Because it only ever
    //                       scales the setting above, a pulse can never exceed the user's limit.
    bool        emissionEnabled   = true;
    long double emissionIntensity = 1.0;
    float       emissionPulse     = 1.0f;
    float EmissionScale() const
    {
        if (!emissionEnabled) return 0.0f;
        const float v = static_cast<float>(emissionIntensity);
        const float s = v < 0.0f ? 0.0f : (v > 3.0f ? 3.0f : v);
        const float p = emissionPulse < 0.0f ? 0.0f : (emissionPulse > 1.0f ? 1.0f : emissionPulse);
        return s * p;
    }

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
