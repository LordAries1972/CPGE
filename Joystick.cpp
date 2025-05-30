#include "Includes.h"
#include "Joystick.h"
#include "Debug.h"
#include <nlohmann/json.hpp>

extern Debug debug;

const std::string lpCONFIG_FILENAME = "GameConfig.cfg";

#pragma warning(push)
#pragma warning(disable: 4101)

Joystick::Joystick() {
    detectJoysticks();
//    loadMapping(lpCONFIG_FILENAME);                           // Not Required for the moment, so we will NOT use, please leave this line here.
}

Joystick::~Joystick() 
{
    if (isDestroyed) return;

//    saveMapping();                                            // Not Required for the moment, so we will NOT use, please leave this line here.
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick Class Destroyed...");
    #endif

    isDestroyed = true;
}

void Joystick::detectJoysticks() {
    activeJoysticks.clear();
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        JOYINFOEX joyInfo = { sizeof(JOYINFOEX), JOY_RETURNALL };
        if (joyGetPosEx(i, &joyInfo) == JOYERR_NOERROR) {
            activeJoysticks.push_back(i);
        }
    }
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Detected " + std::to_wstring(activeJoysticks.size()) + L" joysticks.");
    #endif
}

bool Joystick::readJoystickState(int joystickID, JoystickState& state) {
    if (std::find(activeJoysticks.begin(), activeJoysticks.end(), joystickID) == activeJoysticks.end()) {
        #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid joystick ID: " + std::to_wstring(joystickID));
        #endif
        return false;
    }

    state.joystickID = joystickID;
    state.info.dwSize = sizeof(JOYINFOEX);
    state.info.dwFlags = JOY_RETURNALL;

    if (joyGetPosEx(joystickID, &state.info) != JOYERR_NOERROR) {
        #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read joystick " + std::to_wstring(joystickID));
        #endif
        return false;
    }
    return true;
}

bool Joystick::loadMapping(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to open mapping file: " + std::wstring(filename.begin(), filename.end()));
        #endif
        return false;
    }

    try {
        nlohmann::json jsonData;
        file >> jsonData;

        joystickMappings.clear();
        for (auto& [joystickID, mapping] : jsonData.items()) {
            int id = std::stoi(joystickID);
            for (auto& [button, key] : mapping.items()) {
                joystickMappings[id].buttonToKey[std::stoi(button)] = key.get<int>();
            }
        }
        #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick mapping loaded successfully.");
        #endif
        return true;
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
            std::wstring msg = L"Error parsing mapping file: " + std::wstring(e.what(), e.what() + strlen(e.what()));
            debug.logLevelMessage(LogLevel::LOG_ERROR, msg);
        #endif
        return false;
    }
}

bool Joystick::saveMapping(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to save mapping file: " + std::wstring(filename.begin(), filename.end()));
    #endif
        return false;
    }

    try {
        nlohmann::json jsonData;
        for (auto& [joystickID, mapping] : joystickMappings) {
            for (auto& [button, key] : mapping.buttonToKey) {
                jsonData[std::to_string(joystickID)][std::to_string(button)] = key;
            }
        }

        file << jsonData.dump(4);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick mapping saved successfully.");
        return true;
    }
    catch (const std::exception& e) {
		std::wstring msg = L"Error writing mapping file: " + std::wstring(e.what(), e.what() + strlen(e.what()));
        return false;
    }
}

void Joystick::displayJoystickState(int joystickID) {
    JoystickState state;
    if (!readJoystickState(joystickID, state)) return;

    std::wstringstream output;
    output << L"Joystick " << joystickID << L": X=" << state.info.dwXpos
        << L" Y=" << state.info.dwYpos << L" Z=" << state.info.dwZpos
        << L" Buttons=" << state.info.dwButtons;
    debug.logLevelMessage(LogLevel::LOG_INFO, output.str());
}

void Joystick::processJoystickInput() {
    for (int joystickID : activeJoysticks) {
        JoystickState state;
        if (!readJoystickState(joystickID, state)) continue;

        for (const auto& [button, vkKey] : joystickMappings[joystickID].buttonToKey) {
            if (state.info.dwButtons & (1 << button)) {
                postKeyEvent(vkKey);
            }
        }
    }
}

void Joystick::postKeyEvent(int vkCode) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vkCode);
    input.ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(1, &input, sizeof(INPUT));
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Posted WM_KEYUP event for key: " + std::to_wstring(vkCode));
    #endif
}

//---------------------------------------------------------------------------------
// New Joystick Movement Functions
//---------------------------------------------------------------------------------

float Joystick::normalizeJoystickAxis(DWORD axisValue) {
    // Convert from the raw joystick value [0, 65535] to normalized [-1, 1]
    // with a deadzone in the center
    float normalized = (static_cast<float>(axisValue) - JOYSTICK_CENTER) / JOYSTICK_CENTER;

    // Apply deadzone
    if (std::abs(normalized) < JOYSTICK_DEADZONE / JOYSTICK_CENTER) {
        return 0.0f;
    }

    // Adjust range to account for deadzone
    if (normalized > 0) {
        return (normalized - JOYSTICK_DEADZONE / JOYSTICK_CENTER) /
            (1.0f - JOYSTICK_DEADZONE / JOYSTICK_CENTER);
    }
    else {
        return (normalized + JOYSTICK_DEADZONE / JOYSTICK_CENTER) /
            (1.0f - JOYSTICK_DEADZONE / JOYSTICK_CENTER);
    }
}

JoystickAxes Joystick::getNormalizedAxes(int joystickID) {
    JoystickAxes axes = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    JoystickState state;

    if (!readJoystickState(joystickID, state)) {
        return axes;
    }

    // Normalize all axes
    axes.x = normalizeJoystickAxis(state.info.dwXpos);

    // Y-axis is inverted in most joysticks to match the screen coordinate
    // system (down is positive), but for cameras we want up to be positive
    axes.y = -normalizeJoystickAxis(state.info.dwYpos);

    // Z axis (often used as a throttle or trigger)
    axes.z = normalizeJoystickAxis(state.info.dwZpos);

    // Rotation axes if available
    if (state.info.dwFlags & JOY_RETURNR) {
        axes.rx = normalizeJoystickAxis(state.info.dwRpos);
    }

    if (state.info.dwFlags & JOY_RETURNU) {
        axes.ry = normalizeJoystickAxis(state.info.dwUpos);
    }

    if (state.info.dwFlags & JOY_RETURNV) {
        axes.rz = normalizeJoystickAxis(state.info.dwVpos);
    }

    return axes;
}

void Joystick::processJoystickMovement(int joystickID) {
    if (std::find(activeJoysticks.begin(), activeJoysticks.end(), joystickID) == activeJoysticks.end()) {
        return;  // Not a valid joystick
    }

    JoystickAxes axes = getNormalizedAxes(joystickID);

    // Process movement based on current mode
    if (m_movementMode == MovementMode::MODE_3D && m_camera != nullptr) {
        process3DMovement(axes);
    }
    else {
        process2DMovement(axes);
    }

#if defined(_DEBUG)
    if (std::abs(axes.x) > 0.01f || std::abs(axes.y) > 0.01f) {
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG,
            L"Joystick " + std::to_wstring(joystickID) +
            L" Movement: X=" + std::to_wstring(axes.x) +
            L" Y=" + std::to_wstring(axes.y) +
            L" Z=" + std::to_wstring(axes.z));
    #endif
    }
#endif
}

void Joystick::process3DMovement(const JoystickAxes& axes) {
    if (m_camera == nullptr) {
        return;
    }

    // Left stick: movement (X/Y axes)
    // Right stick: camera rotation (RX/RY axes)

    // Apply forward/backward movement along camera's forward vector
    if (std::abs(axes.y) > 0.01f) {
        float moveDistance = axes.y * m_movementSensitivity;
        if (moveDistance > 0) {
            m_camera->MoveIn(moveDistance);
        }
        else {
            m_camera->MoveOut(-moveDistance);
        }
    }

    // Apply left/right movement
    if (std::abs(axes.x) > 0.01f) {
        float moveDistance = axes.x * m_movementSensitivity;
        if (moveDistance > 0) {
            m_camera->MoveRight(moveDistance);
        }
        else {
            m_camera->MoveLeft(-moveDistance);
        }
    }

    // Apply up/down movement with Z axis (if available)
    if (std::abs(axes.z) > 0.01f) {
        float moveDistance = axes.z * m_movementSensitivity;
        if (moveDistance > 0) {
            m_camera->MoveUp(moveDistance);
        }
        else {
            m_camera->MoveDown(-moveDistance);
        }
    }

    // Process camera rotation (look direction) with right stick
    if (std::abs(axes.rx) > 0.01f || std::abs(axes.ry) > 0.01f) {
        // Get current yaw and pitch
        float yaw = m_camera->m_yaw;
        float pitch = m_camera->m_pitch;

        // Update yaw (horizontal rotation) based on right stick X
        yaw += axes.ry * m_rotationSensitivity;

        // Update pitch (vertical rotation) based on right stick Y
        // Clamp pitch to prevent camera flipping
        pitch += axes.rx * m_rotationSensitivity;
        pitch = std::max(-XM_PIDIV2 + 0.1f, std::min(XM_PIDIV2 - 0.1f, pitch));

        // Update camera look direction
        m_camera->SetYawPitch(yaw, pitch);
    }
}

void Joystick::process2DMovement(const JoystickAxes& axes) {
    // In 2D mode, we simply update the internal 2D position
    // based on joystick input

    // Update position based on normalized axes and sensitivity
    m_last2DPosition.x += axes.x * m_movementSensitivity;
    m_last2DPosition.y += axes.y * m_movementSensitivity;

    // Optional: Clamp values to a specific range if needed for your 2D environment
    // m_last2DPosition.x = std::max(0.0f, std::min(screenWidth, m_last2DPosition.x));
    // m_last2DPosition.y = std::max(0.0f, std::min(screenHeight, m_last2DPosition.y));
}

// Example function for 3D movement with camera
void Joystick::ConfigureFor3DMovement() 
{
    // Set movement mode to 3D
    setMovementMode(MovementMode::MODE_3D);

    // Set sensitivity values suitable for your game
    setMovementSensitivity(0.1f);                                                                       // Movement speed
    setRotationSensitivity(0.01f);                                                                      // Look rotation speed

    // State we are using 3D Mode.
    is3DMode = true;
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick configured for 3D movement with camera");
    #endif
}

// Example function for 2D movement
void Joystick::ConfigureFor2DMovement() 
{
    // Set movement mode to 2D
    setMovementMode(MovementMode::MODE_2D);

    // Set sensitivity value suitable for your 2D game 
    setMovementSensitivity(1.5f);                                                                       // Faster for 2D

    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick configured for 2D movement");
    #endif
}

// Example of switching between 2D and 3D modes
void Joystick::SwitchModes(Camera& camera, bool& isNewMode) 
{
    is3DMode = isNewMode;

    if (is3DMode) 
    {
        setCamera(&camera);
        ConfigureFor3DMovement();
    }
    else 
    {
        ConfigureFor2DMovement();
    }

    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Switched to " + std::wstring(is3DMode ? L"3D" : L"2D") + L" mode");
    #endif
}

#pragma warning(pop)
