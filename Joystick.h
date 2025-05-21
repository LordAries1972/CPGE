#pragma once

#include "Includes.h"
#include "DXCamera.h"
#include "Configuration.h"

const int MAX_JOYSTICKS = 2;
const std::string JoystickMapFilename = "joystick.map";

// Joystick axis value normalization constants
const float JOYSTICK_MAX_VALUE = 65535.0f;
const float JOYSTICK_CENTER = 32767.5f;
const float JOYSTICK_DEADZONE = 4000.0f;                                            // About 6% deadzone

struct JoystickState 
{
    int joystickID;
    JOYINFOEX info;
};

struct ButtonMapping 
{
    std::unordered_map<int, int> buttonToKey;                                       // Joystick button -> Keyboard key
};

enum class MovementMode 
{
    MODE_2D,
    MODE_3D
};

struct JoystickAxes {
    float x;                                                                        // X-axis normalized to [-1.0, 1.0]
    float y;                                                                        // Y-axis normalized to [-1.0, 1.0]
    float z;                                                                        // Z-axis normalized to [-1.0, 1.0]
    float rx;                                                                       // Rotation X-axis (if available)
    float ry;                                                                       // Rotation Y-axis (if available)
    float rz;                                                                       // Rotation Z-axis (if available)
};

class Joystick {
public:
    Joystick();
    ~Joystick();

    bool is3DMode = false;
    bool isDestroyed = false;

    Camera* m_camera = nullptr;

    void detectJoysticks();
    bool readJoystickState(int joystickID, JoystickState& state);
    bool loadMapping(const std::string& filename = JoystickMapFilename);
    bool saveMapping(const std::string& filename = JoystickMapFilename);
    void displayJoystickState(int joystickID);
    void processJoystickInput();
    void ConfigureFor2DMovement();
    void ConfigureFor3DMovement();

    size_t NumOfJoysticks() const { return activeJoysticks.size(); };

    // New joystick movement functions
    void setMovementMode(MovementMode mode) { m_movementMode = mode; }
    MovementMode getMovementMode() const { return m_movementMode; }

    // Set camera reference for 3D mode
    void setCamera(Camera* camera) { m_camera = camera; }
    void SwitchModes(Camera& camera, bool& is3DMode);

    // Process movement from joystick
    void processJoystickMovement(int joystickID = 0);

    // Get normalized joystick axes values
    JoystickAxes getNormalizedAxes(int joystickID = 0);

    // Movement sensitivity settings
    void setMovementSensitivity(float sensitivity) { m_movementSensitivity = sensitivity; }
    void setRotationSensitivity(float sensitivity) { m_rotationSensitivity = sensitivity; }

    // 2D movement coordinates (for 2D mode)
    float getLastX() const { return m_last2DPosition.x; }
    float getLastY() const { return m_last2DPosition.y; }

private:
    std::vector<int> activeJoysticks;
    std::unordered_map<int, ButtonMapping> joystickMappings;

    // New movement-related members
    MovementMode m_movementMode = MovementMode::MODE_3D;
    float m_movementSensitivity = 0.05f;
    float m_rotationSensitivity = 0.02f;

    // Last 2D position (for 2D mode)
    struct Position2D {
        float x = 0.0f;
        float y = 0.0f;
    } m_last2DPosition;

    // Normalize joystick axis with deadzone
    float normalizeJoystickAxis(DWORD axisValue);

    // Handle 3D movement with camera
    void process3DMovement(const JoystickAxes& axes);

    // Handle 2D movement
    void process2DMovement(const JoystickAxes& axes);

    void postKeyEvent(int vkCode);
};

