#include "Includes.h"
#include "Joystick.h"
#include <chrono>

bool Joystick::MenuBackPressed(bool eastDown)
{
    using Clock = std::chrono::steady_clock;
    static bool held = false;
    static Clock::time_point accepted{};
    const bool pressed = eastDown && !held; held = eastDown;
    if (!pressed) return false;
    const auto now = Clock::now();
    if (accepted != Clock::time_point{} && now - accepted < std::chrono::milliseconds(350)) return false;
    accepted = now;
    return true;
}
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

// ---- pad identification ------------------------------------------------------------------------------------------------------------------------------------------
static bool PadNameHas(const std::wstring& name, const wchar_t* part)                  // case-insensitive "contains"
{
    std::wstring a = name, b = part;
    for (wchar_t& c : a) c = static_cast<wchar_t>(::towlower(c));
    for (wchar_t& c : b) c = static_cast<wchar_t>(::towlower(c));
    return a.find(b) != std::wstring::npos;
}
static PadType ClassifyPad(uint16_t vid, uint16_t pid, const std::wstring& name, UINT numButtons, bool hasPov, std::wstring& typeName)
{
    PadType t = PadType::Generic;
    if (vid == 0x054C)                                                                   // Sony
    {
        t = (pid == 0x0268) ? PadType::PlayStation3 : ((pid == 0x0CE6 || pid == 0x0DF2) ? PadType::PlayStation5 : PadType::PlayStation4);
    }
    else if (vid == 0x057E)                                                              // Nintendo
    {
        t = (pid == 0x2006 || pid == 0x2007) ? PadType::JoyCon : PadType::SwitchPro;
    }
    else if (vid == 0x045E)                                                              // Microsoft: the 360 family, or the One / Series family
    {
        t = (pid == 0x02D1 || pid == 0x02DD || pid == 0x02E0 || pid == 0x02EA || pid == 0x02FD || pid == 0x0B00 || pid == 0x0B05 || pid == 0x0B12 || pid == 0x0B13) ? PadType::XboxOne : PadType::Xbox360;
    }
    else if (vid == 0x046D && (pid == 0xC21D || pid == 0xC21E || pid == 0xC21F || pid == 0xC216)) t = PadType::Xbox360;   // Logitech F310 / F510 / F710 (XInput mode)
    else if (PadNameHas(name, L"dualsense")) t = PadType::PlayStation5;                  // no usable IDs: go by the name
    else if (PadNameHas(name, L"dualshock") || PadNameHas(name, L"wireless controller")) t = PadType::PlayStation4;
    else if (PadNameHas(name, L"pro controller")) t = PadType::SwitchPro;
    else if (PadNameHas(name, L"xbox") || PadNameHas(name, L"xinput")) t = PadType::Xbox360;
    (void)numButtons; (void)hasPov;
    typeName = Joystick::PadTypeName(t);
    return t;
}
const wchar_t* Joystick::PadTypeName(PadType t)
{
    switch (t)
    {
    case PadType::Xbox360:       return L"Xbox 360 controller";
    case PadType::XboxOne:       return L"Xbox One / Series controller";
    case PadType::PlayStation3:  return L"DualShock 3";
    case PadType::PlayStation4:  return L"DualShock 4";
    case PadType::PlayStation5:  return L"DualSense";
    case PadType::SwitchPro:     return L"Switch Pro controller";
    case PadType::JoyCon:        return L"Joy-Con";
    case PadType::Generic:       return L"Generic gamepad";
    default:                     return L"Unknown";
    }
}
PadInfo Joystick::GetPadInfo(int slot)
{
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    if (slot < 0 || slot >= MAX_JOYSTICKS) return PadInfo();
    return m_padInfo[static_cast<size_t>(slot)];
}
PadType Joystick::GetPadType(int slot)
{
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    return (slot >= 0 && slot < MAX_JOYSTICKS) ? m_padInfo[static_cast<size_t>(slot)].type : PadType::Unknown;
}
PadInfo Joystick::GetPrimaryPad()
{
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    for (int id : activeJoysticks) if (id >= 0 && id < MAX_JOYSTICKS && m_padInfo[static_cast<size_t>(id)].valid()) return m_padInfo[static_cast<size_t>(id)];
    return PadInfo();
}
// Raw winmm button numbers (1-based, as in JOY_BUTTONn) for each logical button, per family.  Zero = "not a digital button on this pad".
//   Xbox / generic : A B X Y = 1 2 3 4, LB RB = 5 6, Back Start = 7 8, stick clicks 9 10; triggers are the Z axis.
//   DualShock 4/5  : square cross circle triangle = 1 2 3 4, L1 R1 = 5 6, L2 R2 = 7 8, share options = 9 10, stick clicks 11 12, PS button 13.
//   DualShock 3    : select L3 R3 start = 1-4, (d-pad 5-8), L2 R2 L1 R1 = 9-12, triangle circle cross square = 13-16, PS = 17.
//   Switch Pro     : B A Y X = 1 2 3 4, L R = 5 6, ZL ZR = 7 8, minus plus = 9 10, stick clicks 11 12, home 13.
struct PadMap { int south, east, west, north, l1, r1, l2, r2, select, start, l3, r3, guide; };
static const PadMap kMapXbox = { 1, 2, 3, 4, 5, 6, 0, 0, 7, 8, 9, 10, 0 };
static const PadMap kMapPS4  = { 2, 3, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
static const PadMap kMapPS3  = { 15, 14, 16, 13, 11, 12, 9, 10, 1, 4, 2, 3, 17 };
static const PadMap kMapSwitch = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
uint32_t Joystick::ReadPadButtons(int slot, const JOYINFOEX& ji, float* leftTrigger, float* rightTrigger)
{
    const PadType ptype = GetPadType(slot);                                              // (a cheap read: no PadInfo copy with its strings, this runs every frame)
    const PadMap* m = &kMapXbox;
    switch (ptype)
    {
    case PadType::PlayStation4: case PadType::PlayStation5: m = &kMapPS4; break;
    case PadType::PlayStation3: m = &kMapPS3; break;
    case PadType::SwitchPro: case PadType::JoyCon: m = &kMapSwitch; break;
    default: break;
    }
    auto down = [&](int n) { return n > 0 && (ji.dwButtons & (1u << (n - 1))) != 0; };
    uint32_t out = 0;
    if (down(m->south)) out |= kPadSouth;   if (down(m->east)) out |= kPadEast;   if (down(m->west)) out |= kPadWest;   if (down(m->north)) out |= kPadNorth;
    if (down(m->l1)) out |= kPadL1;         if (down(m->r1)) out |= kPadR1;       if (down(m->l2)) out |= kPadL2;       if (down(m->r2)) out |= kPadR2;
    if (down(m->select)) out |= kPadSelect; if (down(m->start)) out |= kPadStart; if (down(m->l3)) out |= kPadL3;       if (down(m->r3)) out |= kPadR3;       if (down(m->guide)) out |= kPadGuide;
    float lt = (out & kPadL2) ? 1.0f : 0.0f, rt = (out & kPadR2) ? 1.0f : 0.0f;
    if (m == &kMapXbox)                                                                  // Xbox-style: both triggers share the Z axis around the centre (left pushes it up, right pulls it down)
    {
        const float z = (static_cast<float>(ji.dwZpos) - JOYSTICK_CENTER) / JOYSTICK_CENTER;
        lt = (z > 0.0f) ? z : 0.0f; rt = (z < 0.0f) ? -z : 0.0f;
        if (lt > 0.35f) out |= kPadL2;
        if (rt > 0.35f) out |= kPadR2;
    }
    if (leftTrigger) *leftTrigger = lt;
    if (rightTrigger) *rightTrigger = rt;
    return out;
}

void Joystick::detectJoysticks() {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);                          // no other thread may be inside winmm or reading the list during a re-scan
    // Build the new list first and swap it in, so the list is never seen half rebuilt (it used to be cleared, then refilled).
    std::vector<int> found;
    found.reserve(MAX_JOYSTICKS);
    std::array<PadInfo, MAX_JOYSTICKS> infos;
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        JOYINFOEX joyInfo = {};
        joyInfo.dwSize = sizeof(JOYINFOEX);
        joyInfo.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(i, &joyInfo) == JOYERR_NOERROR) {
            found.push_back(i);
            // WHAT is it?  The capabilities say how many buttons and axes it has and carry the USB vendor / product IDs.
            JOYCAPSW caps = {};
            if (joyGetDevCapsW(static_cast<UINT_PTR>(i), &caps, sizeof(caps)) == JOYERR_NOERROR) {
                PadInfo& pi = infos[static_cast<size_t>(i)];
                pi.slot = i; pi.vendorId = caps.wMid; pi.productId = caps.wPid; pi.name = caps.szPname;
                pi.numButtons = static_cast<int>(caps.wNumButtons); pi.numAxes = static_cast<int>(caps.wNumAxes);
                pi.hasZ = (caps.wCaps & JOYCAPS_HASZ) != 0; pi.hasR = (caps.wCaps & JOYCAPS_HASR) != 0; pi.hasU = (caps.wCaps & JOYCAPS_HASU) != 0;
                pi.hasV = (caps.wCaps & JOYCAPS_HASV) != 0; pi.hasPov = (caps.wCaps & JOYCAPS_HASPOV) != 0;
                pi.type = ClassifyPad(pi.vendorId, pi.productId, pi.name, caps.wNumButtons, pi.hasPov, pi.typeName);
                pi.triggersOnZ = (pi.type == PadType::Xbox360 || pi.type == PadType::XboxOne || pi.type == PadType::Generic) && pi.hasZ;
            } else {                                                                      // the pad answers but will not describe itself: treat it as a generic one
                PadInfo& pi = infos[static_cast<size_t>(i)];
                pi.slot = i; pi.type = PadType::Generic; pi.typeName = PadTypeName(PadType::Generic);
            }
        }
    }
    // Say so in the log when a slot's pad is new or has changed (plugged in, swapped, replugged) - once, not on every re-scan.
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        const PadInfo& a = m_padInfo[static_cast<size_t>(i)]; const PadInfo& b = infos[static_cast<size_t>(i)];
        if (b.valid() && (!a.valid() || a.vendorId != b.vendorId || a.productId != b.productId || a.type != b.type)) {
            wchar_t ids[32]; _snwprintf_s(ids, _countof(ids), _TRUNCATE, L"VID %04X PID %04X", b.vendorId, b.productId);
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[Joystick] Slot " + std::to_wstring(i) + L": " + b.typeName + L"  (" + ids + L", \"" + b.name + L"\", " +
                                  std::to_wstring(b.numButtons) + L" buttons, " + std::to_wstring(b.numAxes) + L" axes" + (b.triggersOnZ ? L", triggers on the Z axis" : L"") + L")");
        }
    }
    m_padInfo = infos;
    activeJoysticks.swap(found);
    m_activeCount.store(activeJoysticks.size(), std::memory_order_relaxed);
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Detected " + std::to_wstring(activeJoysticks.size()) + L" joysticks.");
    #endif
}

bool Joystick::ReadRawPad(int joystickID, JOYINFOEX& ji) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    ji = {};
    ji.dwSize = sizeof(JOYINFOEX);
    ji.dwFlags = JOY_RETURNALL;
    return joyGetPosEx(static_cast<UINT>(joystickID), &ji) == JOYERR_NOERROR;
}

size_t Joystick::PollControllers() {
    size_t prevCount = m_activeCount.load(std::memory_order_relaxed);
    detectJoysticks();
    size_t newCount = m_activeCount.load(std::memory_order_relaxed);

    if (newCount != prevCount) {
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"[Joystick] Active controllers changed: " +
            std::to_wstring(prevCount) + L" -> " + std::to_wstring(newCount));
    }

    return newCount;
}

bool Joystick::readJoystickState(int joystickID, JoystickState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    if (std::find(activeJoysticks.begin(), activeJoysticks.end(), joystickID) == activeJoysticks.end()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Invalid joystick ID: " + std::to_wstring(joystickID));
        return false;
    }

    state.joystickID = joystickID;
    state.info.dwSize = sizeof(JOYINFOEX);
    state.info.dwFlags = JOY_RETURNALL;

    if (joyGetPosEx(joystickID, &state.info) != JOYERR_NOERROR) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to read joystick " + std::to_wstring(joystickID));
        return false;
    }
    return true;
}

bool Joystick::loadMapping(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to open mapping file: " + std::wstring(filename.begin(), filename.end()));
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
        std::wstring msg = L"Error parsing mapping file: " + std::wstring(e.what(), e.what() + strlen(e.what()));
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, msg);
        return false;
    }
}

bool Joystick::saveMapping(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
    debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"Failed to save mapping file: " + std::wstring(filename.begin(), filename.end()));
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
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);                          // the list must not change under the loop
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
    {
        std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
        if (std::find(activeJoysticks.begin(), activeJoysticks.end(), joystickID) == activeJoysticks.end()) {
            return;  // Not a valid joystick (or just unplugged)
        }
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

    // Apply sensitivity values from configuration
    setMovementSensitivity(static_cast<float>(config.myConfig.joystickSensitivity));
    setRotationSensitivity(static_cast<float>(config.myConfig.joystickRotationSensitivity));

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

    // Apply movement sensitivity from configuration
    setMovementSensitivity(static_cast<float>(config.myConfig.joystickSensitivity));

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
