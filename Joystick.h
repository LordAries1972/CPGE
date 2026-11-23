#pragma once

#include "Includes.h"
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    #include "DXCamera.h"
#elif defined(__USE_OPENGL__)
    #include "OpenGLCamera.h"
#elif defined(__USE_VULKAN__)
    #include "VulkanCamera.h"
#endif
#include "Configuration.h"
#include <atomic>
#include <mutex>
#include <array>
#include <string>

// ---- Joystick error policy -------------------------------------------------------------------------------------------------------------------------------
// A joystick is an optional input device.  Losing one must never end the game.
//
// "Hard point" in this file means any joystick hardware fault that looks
// unrecoverable at the time it is seen: the pad was unplugged, there is no
// driver, the parameters are bad, winmm cannot do what was asked, the
// capabilities query failed, or a read failed.  Every such fault is reported
// as a WARNING through the diagnostics path only (see Debug.h:
// logDiagLevelMessage), never as a higher severity, never by throwing, never
// with a dialog, and never by quitting.  The engine keeps running, falls back
// to keyboard input, and reports zeroed axes for the affected pad.  Each
// warning also updates the last-warning / last-result state and, for a hard
// point result, dumps a one-line status snapshot to DebugLog.txt so the fault
// can be diagnosed after the session.  No function in this file uses the
// fatal UI logging path for a joystick failure.
// ------------------------------------------------------------------------------------------------------------------------------------------------------------

const int MAX_JOYSTICKS = 2;
const std::string JoystickMapFilename = "joystick.map";

// Joystick axis value normalization constants
const float JOYSTICK_MAX_VALUE = 65535.0f;
const float JOYSTICK_CENTER = 32767.5f;
const float JOYSTICK_DEADZONE = 4000.0f;                                            // About 6% deadzone

#if !defined(PLATFORM_WINDOWS)
// Portable winmm stand-ins (Linux/Android only; Windows uses mmsystem.h
// via Includes.h).  Platform.h provides the generic Win32 aliases
// (DWORD, UINT, ...); the winmm-specific types below live here because no
// other header provides them on Linux/Android.  Minimal layout only: the
// Linux stub never reads a real pad, it only needs the types to exist so
// the public API (JoystickState, ReadPadButtons, ReadRawPad) keeps its
// signatures on every platform.  Platform.h must not define JOYINFOEX or
// MMRESULT as well — these are the single definitions.
using MMRESULT = std::uint32_t;
struct JOYINFOEX
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwXpos;
    DWORD dwYpos;
    DWORD dwZpos;
    DWORD dwRpos;
    DWORD dwUpos;
    DWORD dwVpos;
    DWORD dwButtons;
    DWORD dwButtonNumber;
    DWORD dwPOV;
};
#endif
struct JoystickState 
{
    int joystickID;
    JOYINFOEX info;
};

struct ButtonMapping 
{
    std::unordered_map<int, int> buttonToKey;                                       // Joystick button -> Keyboard key
};

// ---- WHAT KIND OF PAD IS IT? ----------------------------------------------------------------------------------------------------------------------------------
// winmm reports every pad as the same "joystick", with buttons numbered in whatever order the driver likes.  The USB vendor / product IDs (and, failing that, the product name) say
// what the pad really is, and each family numbers its buttons differently (an Xbox pad's A is button 1; a PlayStation pad's cross is button 2 and its square button 1).  The engine
// therefore keeps a PadInfo per slot and translates the raw buttons into LOGICAL ones - south / east / west / north (the A B X Y diamond, by position), the shoulders and triggers,
// select / start / the stick clicks - so a game can say "south" and mean the right button on every pad.
enum class PadType { Unknown = 0, Xbox360, XboxOne, PlayStation3, PlayStation4, PlayStation5, SwitchPro, JoyCon, Generic };
enum PadButton : uint32_t
{
    kPadSouth = 1u << 0, kPadEast = 1u << 1, kPadWest = 1u << 2, kPadNorth = 1u << 3,        // Xbox A B X Y / PlayStation cross circle square triangle / Switch B A Y X
    kPadL1 = 1u << 4, kPadR1 = 1u << 5, kPadL2 = 1u << 6, kPadR2 = 1u << 7,                  // shoulders and triggers (the Xbox triggers are an axis: past 35 % counts as pressed)
    kPadSelect = 1u << 8, kPadStart = 1u << 9, kPadL3 = 1u << 10, kPadR3 = 1u << 11, kPadGuide = 1u << 12
};
struct PadInfo
{
    int          slot = -1;
    PadType      type = PadType::Unknown;
    uint16_t     vendorId = 0, productId = 0;                                                // USB IDs (0 when the driver does not say)
    std::wstring name;                                                                       // the product name winmm gives (often a generic driver name)
    std::wstring typeName;                                                                   // "Xbox 360 controller", "DualShock 4", ... - what the engine decided
    int          numButtons = 0, numAxes = 0;
    bool         hasZ = false, hasR = false, hasU = false, hasV = false, hasPov = false;
    bool         triggersOnZ = false;                                                        // an Xbox-style pad: the two triggers share the Z axis (left pushes it up, right pulls it down)
    bool         valid() const { return slot >= 0; }
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

// Linux/Android (Phase A): stub backend — no evdev input yet; methods
// return safe defaults (no controllers, zeroed axes, no key events).
class Joystick {
public:
    // Shared menu navigation: a held east button never skips pages, and a
    // second press is accepted only after the transition cooldown.
    static bool MenuBackPressed(bool eastDown);
    Joystick();
    ~Joystick();

    bool is3DMode = false;
    bool isDestroyed = false;

    Camera* m_camera = nullptr;

    void detectJoysticks();
    size_t PollControllers();                                                        // Re-detect controllers and return active count; call periodically from main loop
    bool readJoystickState(int joystickID, JoystickState& state);
    bool loadMapping(const std::string& filename = JoystickMapFilename);
    bool saveMapping(const std::string& filename = JoystickMapFilename);
    void displayJoystickState(int joystickID);
    void processJoystickInput();
    void ConfigureFor2DMovement();
    void ConfigureFor3DMovement();

    // Thread safe: the main loop re-scans (PollControllers) while the render thread reads the pads every frame, so the count is atomic and every winmm call goes through m_joyMutex.
    size_t NumOfJoysticks() const { return m_activeCount.load(std::memory_order_relaxed); };
    bool HasActiveControllers() const { return m_activeCount.load(std::memory_order_relaxed) != 0; };
    // Pad identity (see PadInfo).  Detected by detectJoysticks() for every slot it finds; copies are returned so another thread can call these while a re-scan runs.
    PadInfo  GetPadInfo(int slot);                                                   // slot < 0 or unplugged: an empty PadInfo (valid() == false)
    PadType  GetPadType(int slot);                                                   // just the type (cheap: use this per frame)
    PadInfo  GetPrimaryPad();                                                        // the first active pad
    // Turns the raw winmm buttons / axes of `ji` into logical PadButton bits for that slot's pad type.  Optionally returns the analog triggers (0..1).
    uint32_t ReadPadButtons(int slot, const JOYINFOEX& ji, float* leftTrigger = nullptr, float* rightTrigger = nullptr);
    static const wchar_t* PadTypeName(PadType t);
    bool ReadRawPad(int joystickID, JOYINFOEX& ji);                                  // locked joyGetPosEx for one slot (false = missing / unplugged); fills dwSize and dwFlags itself

    // Status snapshot for DebugLog.txt.  Writes one line through the diagnostics
    // path (never a dialog, never quits): active count, each slot's pad type /
    // vendor / product / buttons / axes / triggersOnZ, the last winmm result and
    // its text, and the last warning.  Safe to call from any thread.
    void DumpJoystickStatus(const std::wstring& reason);
    // The most recent joystick warning text, or empty when none has been raised.
    std::wstring GetLastJoystickWarning() const;

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
    std::array<PadInfo, MAX_JOYSTICKS> m_padInfo;                                    // what each slot's pad is (guarded by m_joyMutex)
    mutable std::recursive_mutex m_joyMutex;                                         // guards activeJoysticks and every joyGetPosEx call (recursive: readJoystickState is called with it held); mutable so const status accessors can lock it
    std::atomic<size_t> m_activeCount{ 0 };
    std::unordered_map<int, ButtonMapping> joystickMappings;

    // Last joystick fault, kept for the status snapshot.  Updated only by the
    // warning helpers below, under m_joyMutex.  A hard point result is never
    // cleared by a later success, so the snapshot still shows what went wrong.
#if defined(PLATFORM_WINDOWS)
    MMRESULT m_lastWinMmResult = JOYERR_NOERROR;
#else
    MMRESULT m_lastWinMmResult = 0;
#endif
    std::wstring m_lastWarning;

    // Central warning path.  Every joystick failure in this file goes through
    // one of these two helpers, which use the diagnostics path only, so a
    // failure can never show a dialog or quit the application.  The message is
    // written to DebugLog.txt and identical repeats are throttled by the
    // diagnostics layer.  LogJoystickWarningWithResult also records the winmm
    // result and, for a hard point result, dumps a status snapshot.
    void LogJoystickWarning(const std::wstring& message);
    void LogJoystickWarningWithResult(const std::wstring& what, MMRESULT result);
#if !defined(PLATFORM_WINDOWS)
    // Linux/Android stub (Phase A): logs the one-time "joystick input is
    // unavailable on Linux" warning (see Joystick.cpp).  No evdev backend.
    void LogLinuxStubWarningOnce();
#endif
    static std::wstring DescribeWinMmResult(MMRESULT result);
    static bool IsHardPointResult(MMRESULT result);
    std::wstring BuildStatusText(const std::wstring& reason) const;

    // New movement-related members
    MovementMode m_movementMode = MovementMode::MODE_3D;
    float m_movementSensitivity = 0.00000005f;
    float m_rotationSensitivity = 0.00000002f;

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
