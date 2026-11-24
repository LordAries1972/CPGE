#include "Includes.h"
#include "Joystick.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <sstream>
#if defined(PLATFORM_LINUX)
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#endif

// ---- Joystick error policy (see Joystick.h) -----------------------------------------------------------------------------------------------------------------
// Every joystick failure in this file is a WARNING on the diagnostics path
// only: debug.logDiagLevelMessage(LOG_WARNING, ...).  That path writes the
// tagged message to DebugLog.txt, shows it in the in-game console, and never
// shows a dialog or quits, so a lost pad, a missing driver, a bad parameter,
// a failed capabilities query, or a failed read can never end the session.
// The engine keeps running, falls back to keyboard input, and reports zeroed
// axes for the affected pad.  A "hard point" result (unplugged, no driver,
// bad parameters, cannot do, bad device id, not enabled, allocated, or a
// plain winmm failure) additionally dumps a one-line status snapshot to
// DebugLog.txt.  Identical repeated warnings are throttled by the diagnostics
// layer, so a per-frame failure cannot flood the log.
// ------------------------------------------------------------------------------------------------------------------------------------------------------------

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

#if defined(PLATFORM_WINDOWS)
#pragma warning(push)
#pragma warning(disable: 4101)
#endif

// Narrow (char) to wide conversion for std::exception::what() text.  The
// what() string is plain ASCII in practice; an empty result is returned for
// a null pointer instead of dereferencing it.
static std::wstring WideFromNarrow(const char* text)
{
    if (text == nullptr) return std::wstring();
    const size_t len = std::strlen(text);
    return std::wstring(text, text + len);
}

// Sensitivity limits used by ConfigureFor2DMovement / ConfigureFor3DMovement.
// Values outside these ranges are clamped and warned about; a non-finite or
// non-positive value falls back to the matching default below.
static const float kJoystickMoveSensitivityMin = 0.00000001f;
static const float kJoystickMoveSensitivityMax = 10.0f;
static const float kJoystickMoveSensitivityDefault = 0.00000005f;
static const float kJoystickRotSensitivityMin = 0.00000001f;
static const float kJoystickRotSensitivityMax = 1.0f;
static const float kJoystickRotSensitivityDefault = 0.00000002f;

#if defined(PLATFORM_WINDOWS)
std::wstring Joystick::DescribeWinMmResult(MMRESULT result)
{
    std::wostringstream out;
    out << L"0x" << std::hex << std::uppercase << static_cast<unsigned int>(result) << std::dec << L" (";
    switch (result)
    {
    case JOYERR_NOERROR:    out << L"JOYERR_NOERROR: no error"; break;
    case JOYERR_PARMS:      out << L"JOYERR_PARMS: invalid parameter, bad joystick ID, or bad buffer"; break;
    case JOYERR_NOCANDO:    out << L"JOYERR_NOCANDO: winmm cannot do what was asked for this device"; break;
    case JOYERR_UNPLUGGED:  out << L"JOYERR_UNPLUGGED: joystick is unplugged"; break;
    case MMSYSERR_ERROR:    out << L"MMSYSERR_ERROR: unspecified winmm failure"; break;
    case MMSYSERR_BADDEVICEID: out << L"MMSYSERR_BADDEVICEID: bad device ID"; break;
    case MMSYSERR_NOTENABLED:  out << L"MMSYSERR_NOTENABLED: driver is not enabled"; break;
    case MMSYSERR_ALLOCATED:   out << L"MMSYSERR_ALLOCATED: device is already allocated"; break;
    case MMSYSERR_INVALHANDLE: out << L"MMSYSERR_INVALHANDLE: invalid handle"; break;
    case MMSYSERR_NODRIVER:    out << L"MMSYSERR_NODRIVER: no driver is installed for this device"; break;
    case MMSYSERR_NOMEM:       out << L"MMSYSERR_NOMEM: winmm could not allocate memory"; break;
    case MMSYSERR_NOTSUPPORTED: out << L"MMSYSERR_NOTSUPPORTED: function is not supported for this device"; break;
    case MMSYSERR_BADERRNUM:   out << L"MMSYSERR_BADERRNUM: bad error number"; break;
    case MMSYSERR_INVALFLAG:   out << L"MMSYSERR_INVALFLAG: invalid flag"; break;
    case MMSYSERR_INVALPARAM:  out << L"MMSYSERR_INVALPARAM: invalid parameter"; break;
    case MMSYSERR_HANDLEBUSY:  out << L"MMSYSERR_HANDLEBUSY: handle is busy"; break;
    case MMSYSERR_INVALIDALIAS: out << L"MMSYSERR_INVALIDALIAS: invalid alias"; break;
    case MMSYSERR_BADDB:       out << L"MMSYSERR_BADDB: bad database entry"; break;
    case MMSYSERR_KEYNOTFOUND: out << L"MMSYSERR_KEYNOTFOUND: key not found"; break;
    case MMSYSERR_READERROR:   out << L"MMSYSERR_READERROR: read failure"; break;
    case MMSYSERR_WRITEERROR:  out << L"MMSYSERR_WRITEERROR: write failure"; break;
    case MMSYSERR_DELETEERROR: out << L"MMSYSERR_DELETEERROR: delete failure"; break;
    case MMSYSERR_VALNOTFOUND: out << L"MMSYSERR_VALNOTFOUND: value not found"; break;
    case MMSYSERR_NODRIVERCB:  out << L"MMSYSERR_NODRIVERCB: no driver callback"; break;
    default:                   out << L"unknown winmm result"; break;
    }
    out << L")";
    return out.str();
}
#elif defined(PLATFORM_LINUX)
// ---------------------------------------------------------------------------------------------
// Linux evdev result codes.  MMRESULT on Linux is a plain uint32_t (see
// Joystick.h), so the evdev backend defines its own small result space.
// These are only ever used as the "result" in a joystick warning: they are
// translated to text by DescribeWinMmResult (Linux) and classified as a
// hard point by IsHardPointResult (Linux), so a device fault gets the same
// warning-only treatment and status snapshot as a winmm hard point.
// ---------------------------------------------------------------------------------------------
namespace {
constexpr MMRESULT kEvdevOk = 0;              // no error
constexpr MMRESULT kEvdevNoDevice = 1;        // no device at this path / slot empty (not a fault)
constexpr MMRESULT kEvdevOpenFailed = 2;      // open() failed for a reason other than ENOENT
constexpr MMRESULT kEvdevNoPermission = 3;    // open() failed with EACCES (device exists, unreadable)
constexpr MMRESULT kEvdevIoctlFailed = 4;     // an EVIOC* identity / capability query failed
constexpr MMRESULT kEvdevReadFailed = 5;      // read() failed, usually ENODEV (device gone)
constexpr int kEvdevMaxDevices = 32;          // /dev/input/event0 .. event31
constexpr int kEvdevAxisX = 0, kEvdevAxisY = 1, kEvdevAxisZ = 2, kEvdevAxisR = 3, kEvdevAxisU = 4, kEvdevAxisV = 5;  // slot cache order (winmm order)

// evdev absolute-axis code for one slot cache index (ABS_X, ABS_Y, ABS_Z, ABS_RZ, ABS_RX, ABS_RY).
int EvdevAxisCode(int index)
{
    switch (index)
    {
    case kEvdevAxisX: return ABS_X;
    case kEvdevAxisY: return ABS_Y;
    case kEvdevAxisZ: return ABS_Z;
    case kEvdevAxisR: return ABS_RZ;
    case kEvdevAxisU: return ABS_RX;
    case kEvdevAxisV: return ABS_RY;
    default: return ABS_X;
    }
}
} // namespace

std::wstring Joystick::DescribeWinMmResult(MMRESULT result)
{
    std::wostringstream out;
    out << L"0x" << std::hex << std::uppercase << static_cast<unsigned int>(result) << std::dec << L" (";
    switch (result)
    {
    case kEvdevOk:           out << L"EVDEV_OK: no error"; break;
    case kEvdevNoDevice:     out << L"EVDEV_NO_DEVICE: no device at this path"; break;
    case kEvdevOpenFailed:   out << L"EVDEV_OPEN_FAILED: could not open the device node"; break;
    case kEvdevNoPermission: out << L"EVDEV_NO_PERMISSION: device exists but cannot be opened (permission denied)"; break;
    case kEvdevIoctlFailed:  out << L"EVDEV_IOCTL_FAILED: device identity / capability query failed"; break;
    case kEvdevReadFailed:   out << L"EVDEV_READ_FAILED: read failed (device unplugged or gone)"; break;
    default:                 out << L"unknown evdev result"; break;
    }
    out << L")";
    return out.str();
}
#else
std::wstring Joystick::DescribeWinMmResult(MMRESULT result)
{
    // No winmm on Android / macOS / iOS: there is no winmm or evdev result
    // to describe.
    std::wostringstream out;
    out << L"0x" << std::hex << std::uppercase << static_cast<unsigned int>(result) << std::dec << L" (no winmm result on this platform)";
    return out.str();
}
#endif

#if defined(PLATFORM_WINDOWS)
bool Joystick::IsHardPointResult(MMRESULT result)
{
    switch (result)
    {
    case JOYERR_UNPLUGGED:
    case JOYERR_PARMS:
    case JOYERR_NOCANDO:
    case MMSYSERR_ERROR:
    case MMSYSERR_BADDEVICEID:
    case MMSYSERR_NOTENABLED:
    case MMSYSERR_ALLOCATED:
    case MMSYSERR_NODRIVER:
    case MMSYSERR_NOMEM:
        return true;
    default:
        return false;
    }
}
#elif defined(PLATFORM_LINUX)
bool Joystick::IsHardPointResult(MMRESULT result)
{
    // An evdev open / capability / read failure is a hardware fault (the
    // pad is unplugged, unreadable, or gone), so it is a hard point: a
    // warning with a status snapshot, never an error.  "No device" is not
    // a fault — an empty event slot is normal.
    switch (result)
    {
    case kEvdevOpenFailed:
    case kEvdevNoPermission:
    case kEvdevIoctlFailed:
    case kEvdevReadFailed:
        return true;
    default:
        return false;
    }
}
#else
bool Joystick::IsHardPointResult(MMRESULT result)
{
    // No winmm or evdev results exist on Android / macOS / iOS, so no
    // result is a hard point.
    (void)result;
    return false;
}
#endif

#if defined(PLATFORM_WINDOWS)
std::wstring Joystick::BuildStatusText(const std::wstring& reason) const
{
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    std::wostringstream out;
    out << L"STATUS [" << reason << L"] active=" << m_activeCount.load(std::memory_order_relaxed);
    for (int i = 0; i < MAX_JOYSTICKS; ++i)
    {
        const PadInfo& pi = m_padInfo[static_cast<size_t>(i)];
        out << L" | slot " << i << L": ";
        if (!pi.valid())
        {
            out << L"empty";
            continue;
        }
        wchar_t ids[32];
        _snwprintf_s(ids, _countof(ids), _TRUNCATE, L"VID %04X PID %04X", pi.vendorId, pi.productId);
        out << pi.typeName << L" (" << ids << L", " << pi.numButtons << L" buttons, " << pi.numAxes << L" axes"
            << L", triggersOnZ=" << (pi.triggersOnZ ? L"yes" : L"no") << L")";
    }
    out << L" | lastResult=" << DescribeWinMmResult(m_lastWinMmResult);
    out << L" | lastWarning=" << (m_lastWarning.empty() ? L"none" : m_lastWarning);
    return out.str();
}
#else
std::wstring Joystick::BuildStatusText(const std::wstring& reason) const
{
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    std::wostringstream out;
    out << L"STATUS [" << reason << L"] active=" << m_activeCount.load(std::memory_order_relaxed);
    for (int i = 0; i < MAX_JOYSTICKS; ++i)
    {
        const PadInfo& pi = m_padInfo[static_cast<size_t>(i)];
        out << L" | slot " << i << L": ";
        if (!pi.valid())
        {
            out << L"empty";
            continue;
        }
        // Portable VID/PID formatting (no _snwprintf_s on Linux/Android).
        std::wostringstream ids;
        ids << L"VID " << std::hex << std::uppercase << std::setw(4) << std::setfill(L'0') << pi.vendorId
            << L" PID " << std::setw(4) << std::setfill(L'0') << pi.productId;
        out << pi.typeName << L" (" << ids.str() << L", " << pi.numButtons << L" buttons, " << pi.numAxes << L" axes"
            << L", triggersOnZ=" << (pi.triggersOnZ ? L"yes" : L"no") << L")";
    }
    out << L" | lastResult=" << DescribeWinMmResult(m_lastWinMmResult);
    out << L" | lastWarning=" << (m_lastWarning.empty() ? L"none" : m_lastWarning);
    return out.str();
}
#endif

void Joystick::DumpJoystickStatus(const std::wstring& reason)
{
    // One line only, through the diagnostics path: this writes to
    // DebugLog.txt, never shows a dialog, and never quits.  This function
    // deliberately does not call the warning helpers, so it cannot recurse.
    const std::wstring status = BuildStatusText(reason);
    debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"[Joystick] " + status);
}

std::wstring Joystick::GetLastJoystickWarning() const
{
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    return m_lastWarning;
}

void Joystick::LogJoystickWarning(const std::wstring& message)
{
    bool hardPoint = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
        m_lastWarning = message;
        hardPoint = IsHardPointResult(m_lastWinMmResult);
    }
    debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"[Joystick] " + message);
    if (hardPoint)
    {
        DumpJoystickStatus(message);
    }
}

void Joystick::LogJoystickWarningWithResult(const std::wstring& what, MMRESULT result)
{
    {
        std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
        m_lastWinMmResult = result;
    }
    LogJoystickWarning(what + L" (" + DescribeWinMmResult(result) + L")");
}

Joystick::Joystick() {
    try
    {
        detectJoysticks();
    }
    catch (const std::exception& e)
    {
        // Detection must never throw out of the constructor: the engine starts
        // with no controllers and keyboard input instead.
        LogJoystickWarning(L"Joystick constructor: detectJoysticks failed, continuing with no controllers: " + WideFromNarrow(e.what()));
    }
    catch (...)
    {
        LogJoystickWarning(L"Joystick constructor: detectJoysticks failed with an unknown failure, continuing with no controllers");
    }
//    loadMapping(lpCONFIG_FILENAME);                           // Not Required for the moment, so we will NOT use, please leave this line here.
}

Joystick::~Joystick() 
{
    if (isDestroyed) return;

    #if defined(PLATFORM_LINUX)
        // Close the evdev fds so no device node is left open at shutdown.
        // (The engine keeps one Joystick for its whole lifetime, so this
        // only runs at process exit, but a leaked fd is still a leak.)
        {
            std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
            for (int i = 0; i < MAX_JOYSTICKS; ++i) EvdevCloseSlot(i);
        }
    #endif
//    saveMapping();                                            // Not Required for the moment, so we will NOT use, please leave this line here.
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick Class Destroyed...");
    #endif

    isDestroyed = true;
}

// ---- pad identification ------------------------------------------------------------------------------------------------------------------------------------------
// Shared on every platform: the Linux evdev backend classifies pads from
// the same USB IDs and names as the Windows winmm backend, so a pad is the
// same type on Linux and on Windows.
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
// Shared on every platform: the Linux evdev backend translates its buttons
// into the same raw winmm numbering, so ReadPadButtons works unchanged.
struct PadMap { int south, east, west, north, l1, r1, l2, r2, select, start, l3, r3, guide; };
static const PadMap kMapXbox = { 1, 2, 3, 4, 5, 6, 0, 0, 7, 8, 9, 10, 0 };
static const PadMap kMapPS4  = { 2, 3, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
static const PadMap kMapPS3  = { 15, 14, 16, 13, 11, 12, 9, 10, 1, 4, 2, 3, 17 };
static const PadMap kMapSwitch = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
// The PadMap for one pad type (shared by ReadPadButtons and the Linux
// evdev button translation).
static const PadMap* PadMapFor(PadType type)
{
    switch (type)
    {
    case PadType::PlayStation4: case PadType::PlayStation5: return &kMapPS4;
    case PadType::PlayStation3: return &kMapPS3;
    case PadType::SwitchPro: case PadType::JoyCon: return &kMapSwitch;
    default: return &kMapXbox;
    }
}
uint32_t Joystick::ReadPadButtons(int slot, const JOYINFOEX& ji, float* leftTrigger, float* rightTrigger)
{
    if (slot < 0 || slot >= MAX_JOYSTICKS)
    {
        if (leftTrigger) *leftTrigger = 0.0f;
        if (rightTrigger) *rightTrigger = 0.0f;
        LogJoystickWarning(L"ReadPadButtons: invalid slot " + std::to_wstring(slot) + L", expected 0.." + std::to_wstring(MAX_JOYSTICKS - 1) + L", returning no buttons");
        return 0;
    }
    const PadInfo pad = GetPadInfo(slot);
    const PadType ptype = pad.type;
    const PadMap* m = PadMapFor(ptype);
    auto down = [&](int n) { return n > 0 && (ji.dwButtons & (1u << (n - 1))) != 0; };
    uint32_t out = 0;
    if (down(m->south)) out |= kPadSouth;   if (down(m->east)) out |= kPadEast;   if (down(m->west)) out |= kPadWest;   if (down(m->north)) out |= kPadNorth;
    if (down(m->l1)) out |= kPadL1;         if (down(m->r1)) out |= kPadR1;       if (down(m->l2)) out |= kPadL2;       if (down(m->r2)) out |= kPadR2;
    if (down(m->select)) out |= kPadSelect; if (down(m->start)) out |= kPadStart; if (down(m->l3)) out |= kPadL3;       if (down(m->r3)) out |= kPadR3;       if (down(m->guide)) out |= kPadGuide;
    float lt = (out & kPadL2) ? 1.0f : 0.0f, rt = (out & kPadR2) ? 1.0f : 0.0f;
    if (m == &kMapXbox && pad.triggersOnZ)                                               // Xbox-style: both triggers share the Z axis around the centre (left pushes it up, right pulls it down)
    {
        const float rawZ = std::clamp(static_cast<float>(ji.dwZpos), 0.0f, JOYSTICK_MAX_VALUE);
        const float z = std::clamp((rawZ - JOYSTICK_CENTER) / JOYSTICK_CENTER, -1.0f, 1.0f);
        lt = (z > 0.0f) ? z : 0.0f; rt = (z < 0.0f) ? -z : 0.0f;
        if (lt > 0.35f) out |= kPadL2;
        if (rt > 0.35f) out |= kPadR2;
    }
    // When triggersOnZ is false the Z axis is not a trigger axis: a dwZpos of
    // 0 must not be read as a fully pressed right trigger.  lt / rt keep the
    // digital L2 / R2 values computed above (0 for the Xbox map, where L2 and
    // R2 are not digital buttons).
    if (leftTrigger) *leftTrigger = lt;
    if (rightTrigger) *rightTrigger = rt;
    return out;
}

#if defined(PLATFORM_WINDOWS)
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
        const MMRESULT posResult = joyGetPosEx(static_cast<UINT>(i), &joyInfo);
        if (posResult != JOYERR_NOERROR) {
            // An empty slot that never had a pad stays silent: no pad there is
            // normal, not a fault.  A slot that had a pad and now fails is a
            // hard point fault (usually unplugged) and is warned about once
            // here, with a status snapshot, instead of every frame.
            if (m_padInfo[static_cast<size_t>(i)].valid()) {
                LogJoystickWarningWithResult(L"[Joystick] Slot " + std::to_wstring(i) + L" pad lost during re-scan (was " + m_padInfo[static_cast<size_t>(i)].typeName + L"), continuing with keyboard fallback; zeroed axes", posResult);
            }
            continue;
        }
        found.push_back(i);
        // WHAT is it?  The capabilities say how many buttons and axes it has and carry the USB vendor / product IDs.
        JOYCAPSW caps = {};
        const MMRESULT capsResult = joyGetDevCapsW(static_cast<UINT_PTR>(i), &caps, sizeof(caps));
        if (capsResult == JOYERR_NOERROR) {
            PadInfo& pi = infos[static_cast<size_t>(i)];
            pi.slot = i; pi.vendorId = caps.wMid; pi.productId = caps.wPid; pi.name = caps.szPname;
            pi.numButtons = static_cast<int>(caps.wNumButtons); pi.numAxes = static_cast<int>(caps.wNumAxes);
            pi.hasZ = (caps.wCaps & JOYCAPS_HASZ) != 0; pi.hasR = (caps.wCaps & JOYCAPS_HASR) != 0; pi.hasU = (caps.wCaps & JOYCAPS_HASU) != 0;
            pi.hasV = (caps.wCaps & JOYCAPS_HASV) != 0; pi.hasPov = (caps.wCaps & JOYCAPS_HASPOV) != 0;
            pi.type = ClassifyPad(pi.vendorId, pi.productId, pi.name, caps.wNumButtons, pi.hasPov, pi.typeName);
            pi.triggersOnZ = (pi.type == PadType::Xbox360 || pi.type == PadType::XboxOne || pi.type == PadType::Generic) && pi.hasZ;
        } else {                                                                      // the pad answers but will not describe itself: treat it as a generic one
            LogJoystickWarningWithResult(L"[Joystick] Slot " + std::to_wstring(i) + L" capabilities query failed, treating the pad as Generic", capsResult);
            PadInfo& pi = infos[static_cast<size_t>(i)];
            pi.slot = i; pi.type = PadType::Generic; pi.typeName = PadTypeName(PadType::Generic);
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
        if (a.valid() && !b.valid()) {
            wchar_t ids[32]; _snwprintf_s(ids, _countof(ids), _TRUNCATE, L"VID %04X PID %04X", a.vendorId, a.productId);
            const std::wstring removed = L"[Joystick] Slot " + std::to_wstring(i) + L" pad removed (" + a.typeName + L", " + ids + L"), continuing with keyboard fallback; zeroed axes";
            LogJoystickWarning(removed);
            DumpJoystickStatus(removed);
        }
    }
    m_padInfo = infos;
    activeJoysticks.swap(found);
    m_activeCount.store(activeJoysticks.size(), std::memory_order_relaxed);
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Detected " + std::to_wstring(activeJoysticks.size()) + L" joysticks.");
    #endif
}
#elif defined(PLATFORM_LINUX)
void Joystick::detectJoysticks() {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);                          // no other thread may be inside evdev or reading the list during a re-scan
    // Keep the old pad list for the new/removed logging below (same shape as
    // the Windows re-scan: build the new state, then compare and log).
    const std::array<PadInfo, MAX_JOYSTICKS> oldInfos = m_padInfo;
    // 1) Probe the slots that already have a device: a pad that was
    //    unplugged stops answering its name query (ENODEV).  The slot is
    //    closed here; the removal is warned about once in step 3 below.
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        if (m_evdev[static_cast<size_t>(i)].fd >= 0 && !EvdevProbeSlot(i)) {
            EvdevCloseSlot(i);
        }
    }
    // 2) Scan /dev/input/event0..31 for gamepads that are not open yet.
    //    PollControllers() throttles this to once per 3 seconds, so opening
    //    a node per index is cheap.  A node that is not there (ENOENT) is
    //    normal and silent; a keyboard or mouse is closed again silently.
    //    Only a real gamepad takes a slot.
    for (int n = 0; n < kEvdevMaxDevices; ++n) {
        const std::string path = "/dev/input/event" + std::to_string(n);
        bool alreadyOpen = false;
        for (int i = 0; i < MAX_JOYSTICKS; ++i) {
            if (m_evdev[static_cast<size_t>(i)].fd >= 0 && m_evdev[static_cast<size_t>(i)].path == path) { alreadyOpen = true; break; }
        }
        if (alreadyOpen) continue;
        int freeSlot = -1;
        for (int i = 0; i < MAX_JOYSTICKS; ++i) {
            if (m_evdev[static_cast<size_t>(i)].fd < 0) { freeSlot = i; break; }
        }
        if (freeSlot < 0) break;                                                    // no free slot: further pads are ignored
        EvdevOpenSlot(freeSlot, path);                                              // logs its own warnings; false = not a gamepad / not openable
    }
    // 3) Say so in the log when a slot's pad is new or has changed (plugged
    //    in, swapped, replugged) - once, not on every re-scan.  A removed pad
    //    is a hard point fault: warned about with a status snapshot.
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        const PadInfo& a = oldInfos[static_cast<size_t>(i)]; const PadInfo& b = m_padInfo[static_cast<size_t>(i)];
        if (b.valid() && (!a.valid() || a.vendorId != b.vendorId || a.productId != b.productId || a.type != b.type)) {
            std::wostringstream ids;
            ids << L"VID " << std::hex << std::uppercase << std::setw(4) << std::setfill(L'0') << b.vendorId
                << L" PID " << std::setw(4) << std::setfill(L'0') << b.productId;
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[Joystick] Slot " + std::to_wstring(i) + L": " + b.typeName + L"  (" + ids.str() + L", \"" + b.name + L"\", " +
                                  std::to_wstring(b.numButtons) + L" buttons, " + std::to_wstring(b.numAxes) + L" axes" + (b.triggersOnZ ? L", triggers on the Z axis" : L"") + L")");
        }
        if (a.valid() && !b.valid()) {
            std::wostringstream ids;
            ids << L"VID " << std::hex << std::uppercase << std::setw(4) << std::setfill(L'0') << a.vendorId
                << L" PID " << std::setw(4) << std::setfill(L'0') << a.productId;
            const std::wstring removed = L"[Joystick] Slot " + std::to_wstring(i) + L" pad removed (" + a.typeName + L", " + ids.str() + L"), continuing with keyboard fallback; zeroed axes";
            LogJoystickWarning(removed);
            DumpJoystickStatus(removed);
        }
    }
    std::vector<int> found;
    found.reserve(MAX_JOYSTICKS);
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        if (m_evdev[static_cast<size_t>(i)].fd >= 0) found.push_back(i);
    }
    activeJoysticks.swap(found);
    m_activeCount.store(activeJoysticks.size(), std::memory_order_relaxed);
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Detected " + std::to_wstring(activeJoysticks.size()) + L" joysticks.");
    #endif
}
#elif defined(PLATFORM_ANDROID)
void Joystick::detectJoysticks() {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    // Android: the NDK exposes no gamepad enumeration API, so a slot is
    // reported while any gamepad event has been seen for it
    // (HandleAndroidInputEvent sets AndroidSlot::seen and fills the slot's
    // PadInfo on first sight).  A slot that has never seen a gamepad event
    // is not a fault — no pad attached is normal — so this stays silent,
    // like the evdev scan.
    std::vector<int> found;
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        if (m_android[static_cast<size_t>(i)].seen) found.push_back(i);
    }
    activeJoysticks.swap(found);
    m_activeCount.store(activeJoysticks.size(), std::memory_order_relaxed);
}
#else
void Joystick::detectJoysticks() {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    // macOS / iOS: no joystick backend yet, so no pad is ever
    // found.  Clear any previous state and report zero active controllers.
    // Deliberately silent: the feature is disabled on this platform, not
    // stubbed, so there is nothing to warn about.
    activeJoysticks.clear();
    m_padInfo = std::array<PadInfo, MAX_JOYSTICKS>{};
    m_activeCount.store(0, std::memory_order_relaxed);
}
#endif

#if defined(PLATFORM_WINDOWS)
bool Joystick::ReadRawPad(int joystickID, JOYINFOEX& ji) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    if (joystickID < 0 || joystickID >= MAX_JOYSTICKS) {
        LogJoystickWarning(L"ReadRawPad: invalid joystick ID " + std::to_wstring(joystickID) + L", expected 0.." + std::to_wstring(MAX_JOYSTICKS - 1));
        ji = {};
        return false;
    }
    ji = {};
    ji.dwSize = sizeof(JOYINFOEX);
    ji.dwFlags = JOY_RETURNALL;
    const MMRESULT result = joyGetPosEx(static_cast<UINT>(joystickID), &ji);
    if (result != JOYERR_NOERROR) {
        LogJoystickWarningWithResult(L"ReadRawPad: failed to read joystick " + std::to_wstring(joystickID), result);
        return false;
    }
    return true;
}
#elif defined(PLATFORM_LINUX)
bool Joystick::ReadRawPad(int joystickID, JOYINFOEX& ji) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    if (joystickID < 0 || joystickID >= MAX_JOYSTICKS) {
        LogJoystickWarning(L"ReadRawPad: invalid joystick ID " + std::to_wstring(joystickID) + L", expected 0.." + std::to_wstring(MAX_JOYSTICKS - 1));
        ji = {};
        return false;
    }
    EvdevSlot& slot = m_evdev[static_cast<size_t>(joystickID)];
    if (slot.fd < 0) {
        // No pad in this slot: normal, not a fault, so no warning.
        ji = {};
        return false;
    }
    ji = {};
    ji.dwSize = sizeof(JOYINFOEX);
    ji.dwFlags = JOY_RETURNALL;
    if (!EvdevDrainSlot(joystickID)) {
        // The device stopped answering mid-read (usually unplugged): a hard
        // point fault, warned about with a status snapshot, and the slot is
        // closed so the next re-scan can pick the pad up again if it returns.
        EvdevCloseSlot(joystickID);
        activeJoysticks.erase(std::remove(activeJoysticks.begin(), activeJoysticks.end(), joystickID), activeJoysticks.end());
        m_activeCount.store(activeJoysticks.size(), std::memory_order_relaxed);
        LogJoystickWarningWithResult(L"ReadRawPad: failed to read joystick " + std::to_wstring(joystickID) + L" (device gone), continuing with keyboard fallback; zeroed axes", kEvdevReadFailed);
        return false;
    }
    EvdevFillJoyInfo(joystickID, ji);
    return true;
}
#elif defined(PLATFORM_ANDROID)
bool Joystick::ReadRawPad(int joystickID, JOYINFOEX& ji) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    if (joystickID < 0 || joystickID >= MAX_JOYSTICKS) {
        LogJoystickWarning(L"ReadRawPad: invalid joystick ID " + std::to_wstring(joystickID) + L", expected 0.." + std::to_wstring(MAX_JOYSTICKS - 1));
        ji = {};
        return false;
    }
    if (!m_android[static_cast<size_t>(joystickID)].seen) {
        // No gamepad event seen for this slot yet: no pad, not a fault.
        ji = {};
        return false;
    }
    AndroidFillJoyInfo(joystickID, ji);
    return true;
}
#else
bool Joystick::ReadRawPad(int joystickID, JOYINFOEX& ji) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    (void)joystickID;
    ji = {};
    // macOS / iOS: no joystick backend yet, so no pad is ever read.
    // Deliberately silent (disabled, not stubbed).
    return false;
}
#endif

#if defined(PLATFORM_ANDROID)
// ---- Android AInputEvent backend (see Joystick.h) ---------------------------
// Gamepad events arrive through HandleAndroidInputEvent (called by main.cpp's
// glue onInputEvent callback on the engine thread) and update the cached
// AndroidSlot; AndroidFillJoyInfo translates the cache into the winmm
// JOYINFOEX the game code already understands, mirroring EvdevFillJoyInfo.
// Android reports the two triggers as separate axes (AXIS_LTRIGGER /
// AXIS_RTRIGGER, 0 at rest to 1 fully pressed), so they are combined into
// the winmm Z axis around the centre (Xbox-style, triggersOnZ) exactly as
// the evdev backend does for ABS_Z / ABS_RZ.  Slots are per-deviceId
// (Phase D step 3): HandleAndroidInputEvent claims a free slot for the
// first event from a new device id and routes later events from that
// device to the same slot.  Every failure here is a WARNING on the
// diagnostics path only (see the hard-point rule in Joystick.h): a
// missing pad is silent, never a dialog or quit.
// ----------------------------------------------------------------------------
// The hat (d-pad) as a winmm POV angle: hundredths of a degree clockwise
// from up, or JOY_POVCENTERED when the hat is centred.  |hat| >= 0.5 counts
// as pressed, so both the -1/0/1 hat axes and the DPAD keys (which set the
// same pair) read the same way.
static DWORD AndroidHatToPov(float hatX, float hatY)
{
    const int x = (hatX >= 0.5f) ? 1 : (hatX <= -0.5f) ? -1 : 0;
    const int y = (hatY >= 0.5f) ? 1 : (hatY <= -0.5f) ? -1 : 0;
    if (x == 0 && y == 0) return JOY_POVCENTERED;
    if (x == 0 && y < 0) return 0;
    if (x > 0 && y < 0) return 4500;
    if (x > 0 && y == 0) return 9000;
    if (x > 0 && y > 0) return 13500;
    if (x == 0 && y > 0) return 18000;
    if (x < 0 && y > 0) return 22500;
    if (x < 0 && y == 0) return 27000;
    return 31500;
}

void Joystick::AndroidFillJoyInfo(int slot, JOYINFOEX& ji) const
{
    const AndroidSlot& s = m_android[static_cast<size_t>(slot)];
    const PadInfo& pi = m_padInfo[static_cast<size_t>(slot)];
    ji = {};
    ji.dwSize = sizeof(JOYINFOEX);
    ji.dwFlags = JOY_RETURNALL;
    // One axis: -1..1 -> the winmm [0, 65535] range.  An axis at rest reads
    // as the centre, so getNormalizedAxes never sees a false deflection.
    auto axis01 = [](float v) -> DWORD {
        return static_cast<DWORD>(std::clamp((v + 1.0f) * 32767.5f, 0.0f, 65535.0f));
    };
    ji.dwXpos = axis01(s.axisX);
    ji.dwYpos = axis01(s.axisY);
    // Triggers on the Z axis (Xbox-style): combine the two 0..1 trigger
    // axes into one axis around the centre, so a pad at rest reads as the
    // centre instead of a fully pressed trigger.  dwRpos is the centre:
    // there is no R rotation axis on such a pad.
    ji.dwZpos = static_cast<DWORD>(std::clamp(32767.5f + (s.triggerL - s.triggerR) * 32767.5f, 0.0f, 65535.0f));
    ji.dwRpos = 32767;
    ji.dwUpos = axis01(s.axisZ);    // right stick X (AXIS_Z)
    ji.dwVpos = axis01(s.axisRz);   // right stick Y (AXIS_RZ)
    // Buttons: translate the logical PadButtons mask into the raw winmm
    // numbering for this pad's family, using the same PadMap as
    // ReadPadButtons, so a game sees the same raw buttons on Android as it
    // does on Windows.
    const PadMap* m = PadMapFor(pi.type);
    auto raw = [](int n) { return n > 0 ? (1u << (n - 1)) : 0u; };
    DWORD buttons = 0;
    if (s.buttonMask & kPadSouth)  buttons |= raw(m->south);
    if (s.buttonMask & kPadEast)   buttons |= raw(m->east);
    if (s.buttonMask & kPadWest)   buttons |= raw(m->west);
    if (s.buttonMask & kPadNorth)  buttons |= raw(m->north);
    if (s.buttonMask & kPadL1)     buttons |= raw(m->l1);
    if (s.buttonMask & kPadR1)     buttons |= raw(m->r1);
    if (s.buttonMask & kPadL2)     buttons |= raw(m->l2);
    if (s.buttonMask & kPadR2)     buttons |= raw(m->r2);
    if (s.buttonMask & kPadSelect) buttons |= raw(m->select);
    if (s.buttonMask & kPadStart)  buttons |= raw(m->start);
    if (s.buttonMask & kPadL3)     buttons |= raw(m->l3);
    if (s.buttonMask & kPadR3)     buttons |= raw(m->r3);
    if (s.buttonMask & kPadGuide)  buttons |= raw(m->guide);
    ji.dwButtons = buttons;
    ji.dwPOV = AndroidHatToPov(s.hatX, s.hatY);
}

bool Joystick::HandleAndroidInputEvent(AInputEvent* event)
{
    const int32_t type = AInputEvent_getType(event);
    const int32_t source = AInputEvent_getSource(event);
    const bool isGamepad = (source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK | AINPUT_SOURCE_DPAD)) != 0;
    if (!isGamepad)
        return false;   // Keyboard keys and touchscreen belong to the KeyboardHandler / GUI

    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    // Per-device slots (Phase D step 3): the NDK exposes no gamepad
    // enumeration API, but every event carries its device id, so the
    // first event from a new device claims a free slot and later events
    // from the same device find it again. A device that disconnects keeps
    // its slot (its last state stays cached) until a new device needs it.
    const int32_t deviceId = AInputEvent_getDeviceId(event);
    int slot = -1;
    for (int i = 0; i < MAX_JOYSTICKS; ++i) {
        if (m_android[static_cast<size_t>(i)].seen &&
            m_android[static_cast<size_t>(i)].deviceId == deviceId) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < MAX_JOYSTICKS; ++i) {
            if (!m_android[static_cast<size_t>(i)].seen) { slot = i; break; }
        }
    }
    if (slot < 0) {
        // Every slot is taken by another pad, so this device cannot be
        // tracked. A warning only (the diagnostics layer throttles
        // repeats); the event is dropped, never a dialog or quit.
        LogJoystickWarning(L"Joystick: Android gamepad event from device " + std::to_wstring(deviceId) + L" ignored — no free joystick slot");
        return false;
    }
    AndroidSlot& s = m_android[static_cast<size_t>(slot)];
    // First gamepad event for this slot: describe the pad.  Android gives
    // no vendor / product IDs through the NDK, so it is Generic; the
    // triggers ride the Z axis (Xbox-style) and the right stick is U/V.
    if (!s.seen) {
        s.seen = true;
        s.deviceId = deviceId;
        PadInfo& pi = m_padInfo[static_cast<size_t>(slot)];
        pi.slot = slot; pi.type = PadType::Generic; pi.typeName = PadTypeName(PadType::Generic);
        pi.name = L"Android gamepad"; pi.numButtons = 13; pi.numAxes = 6;
        pi.hasZ = true; pi.hasR = false; pi.hasU = true; pi.hasV = true; pi.hasPov = true;
        pi.triggersOnZ = true;
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[Joystick] Android gamepad detected (slot " + std::to_wstring(slot) + L", device " + std::to_wstring(deviceId) + L", Generic)");
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        const int32_t akey = AKeyEvent_getKeyCode(event);
        const int32_t action = AKeyEvent_getAction(event);
        if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP)
            return false;
        const bool down = (action == AKEY_EVENT_ACTION_DOWN);
        uint32_t bit = 0;
        uint32_t dpadBit = 0;
        switch (akey) {
            case AKEYCODE_BUTTON_A:      bit = kPadSouth; break;
            case AKEYCODE_BUTTON_B:      bit = kPadEast; break;
            case AKEYCODE_BUTTON_X:      bit = kPadWest; break;
            case AKEYCODE_BUTTON_Y:      bit = kPadNorth; break;
            case AKEYCODE_BUTTON_L1:     bit = kPadL1; break;
            case AKEYCODE_BUTTON_R1:     bit = kPadR1; break;
            case AKEYCODE_BUTTON_L2:     bit = kPadL2; break;
            case AKEYCODE_BUTTON_R2:     bit = kPadR2; break;
            case AKEYCODE_BUTTON_SELECT: bit = kPadSelect; break;
            case AKEYCODE_BUTTON_START:  bit = kPadStart; break;
            case AKEYCODE_BUTTON_THUMBL: bit = kPadL3; break;
            case AKEYCODE_BUTTON_THUMBR: bit = kPadR3; break;
            case AKEYCODE_BUTTON_MODE:   bit = kPadGuide; break;
            case AKEYCODE_DPAD_UP:       dpadBit = 1u << 0; break;
            case AKEYCODE_DPAD_RIGHT:    dpadBit = 1u << 1; break;
            case AKEYCODE_DPAD_DOWN:     dpadBit = 1u << 2; break;
            case AKEYCODE_DPAD_LEFT:     dpadBit = 1u << 3; break;
            default: return false;   // not a gamepad button (e.g. a keyboard key with a gamepad source)
        }
        if (bit != 0) {
            if (down) s.buttonMask |= bit; else s.buttonMask &= ~bit;
        }
        else {
            if (down) s.dpadMask |= dpadBit; else s.dpadMask &= ~dpadBit;
            // The DPAD keys and the hat axes share one hat pair: recompute
            // it from the key mask whenever a DPAD key moves.
            s.hatX = ((s.dpadMask & (1u << 1)) ? 1.0f : 0.0f) - ((s.dpadMask & (1u << 3)) ? 1.0f : 0.0f);
            s.hatY = ((s.dpadMask & (1u << 2)) ? 1.0f : 0.0f) - ((s.dpadMask & (1u << 0)) ? 1.0f : 0.0f);
        }
        return true;
    }

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        // Sticks and triggers, pointer 0 (the first finger / stick).
        s.axisX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
        s.axisY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
        s.axisZ = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
        s.axisRz = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
        s.triggerL = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
        s.triggerR = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
        s.hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
        s.hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
        return true;
    }
    return false;
}
#endif // PLATFORM_ANDROID

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

#if defined(PLATFORM_LINUX)
// ---- Linux evdev backend (see Joystick.h) --------------------------------------------------------------------------------------------------------------------
// The kernel exposes every input device as /dev/input/eventN.  A gamepad is
// a node that reports absolute axes (ABS_X / ABS_Y at least) or gamepad
// buttons (BTN_SOUTH and friends).  The fd is opened non-blocking and kept
// open across re-scans, so the cached axis/button state survives a hotplug
// re-scan; events are drained into that cache by EvdevDrainSlot, and
// EvdevFillJoyInfo translates the cache into the winmm JOYINFOEX the game
// code already understands.  Every failure is a WARNING on the diagnostics
// path only (see the hard-point rule in Joystick.h): an open / ioctl / read
// failure warns once and dumps a status snapshot, never a dialog or quit.
// A node that is not there (ENOENT) or not a gamepad is normal and silent.
// ------------------------------------------------------------------------------------------------------------------------------------------------------------
namespace {
// Is bit `code` set in an EVIOCGBIT / EVIOCGKEY byte buffer?
bool EvdevBit(const unsigned char* bits, int code)
{
    return (bits[code / 8] >> (code % 8)) & 1u;
}
// How many bits are set in `bits` over [first, last]?
int EvdevCountBits(const unsigned char* bits, int first, int last)
{
    int count = 0;
    for (int code = first; code <= last; ++code) if (EvdevBit(bits, code)) ++count;
    return count;
}
// One evdev axis value (0..1), from the range the device reports.  A device
// that reports no range (max <= min) reads as 0.
float EvdevAxis01(int value, int minimum, int maximum)
{
    if (maximum <= minimum) return 0.0f;
    const int clamped = std::clamp(value, minimum, maximum);
    return static_cast<float>(clamped - minimum) / static_cast<float>(maximum - minimum);
}
// The hat (ABS_HAT0X/Y, each -1 / 0 / 1) as a winmm POV angle: hundredths of
// a degree clockwise from up, or JOY_POVCENTERED when the hat is centred.
DWORD EvdevHatToPov(int hatX, int hatY)
{
    if (hatX == 0 && hatY == 0) return JOY_POVCENTERED;
    if (hatX == 0 && hatY < 0) return 0;
    if (hatX > 0 && hatY < 0) return 4500;
    if (hatX > 0 && hatY == 0) return 9000;
    if (hatX > 0 && hatY > 0) return 13500;
    if (hatX == 0 && hatY > 0) return 18000;
    if (hatX < 0 && hatY > 0) return 22500;
    if (hatX < 0 && hatY == 0) return 27000;
    return 31500;  // hatX < 0 && hatY < 0
}
} // namespace

bool Joystick::EvdevOpenSlot(int slot, const std::string& path)
{
    // Caller holds m_joyMutex and has already found a free slot.
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const int err = errno;
        if (err == ENOENT) return false;                                          // no device at this index: normal, silent
        if (err == EACCES) {
            // A node exists but cannot be opened.  This is reported once per
            // process, not per node: on a machine where the user cannot read
            // /dev/input at all, every node fails, and the message must not
            // claim a gamepad failed — an unreadable node might be a keyboard.
            // It is a plain warning (no status dump): no pad is known yet.
            static bool permissionWarned = false;
            if (!permissionWarned) {
                permissionWarned = true;
                LogJoystickWarning(L"Joystick detection: cannot open " + std::wstring(path.begin(), path.end()) + L" (permission denied); input devices may be unreadable — check that the user is in the 'input' group, joystick input may be unavailable");
            }
            return false;
        }
        if (m_evdevWarnedPaths.insert(path).second) {
            LogJoystickWarningWithResult(L"Joystick detection: cannot open " + std::wstring(path.begin(), path.end()) + L" (" + WideFromNarrow(std::strerror(err)) + L")", kEvdevOpenFailed);
        }
        return false;
    }
    // Identity: the kernel name and the USB vendor / product IDs.
    char name[256] = {};
    if (::ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        const int err = errno;
        ::close(fd);
        if (m_evdevWarnedPaths.insert(path).second) {
            LogJoystickWarningWithResult(L"Joystick detection: cannot query the name of " + std::wstring(path.begin(), path.end()) + L" (" + WideFromNarrow(std::strerror(err)) + L")", kEvdevIoctlFailed);
        }
        return false;
    }
    struct input_id id = {};
    if (::ioctl(fd, EVIOCGID, &id) < 0) {
        const int err = errno;
        ::close(fd);
        if (m_evdevWarnedPaths.insert(path).second) {
            LogJoystickWarningWithResult(L"Joystick detection: cannot query the identity of " + std::wstring(path.begin(), path.end()) + L" (" + WideFromNarrow(std::strerror(err)) + L")", kEvdevIoctlFailed);
        }
        return false;
    }
    // Capabilities: which absolute axes and which keys this device has.
    unsigned char absBits[(ABS_MAX + 7) / 8] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
        const int err = errno;
        ::close(fd);
        if (m_evdevWarnedPaths.insert(path).second) {
            LogJoystickWarningWithResult(L"Joystick detection: cannot query the axes of " + std::wstring(path.begin(), path.end()) + L" (" + WideFromNarrow(std::strerror(err)) + L")", kEvdevIoctlFailed);
        }
        return false;
    }
    unsigned char keyBits[(KEY_MAX + 7) / 8] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) {
        const int err = errno;
        ::close(fd);
        if (m_evdevWarnedPaths.insert(path).second) {
            LogJoystickWarningWithResult(L"Joystick detection: cannot query the buttons of " + std::wstring(path.begin(), path.end()) + L" (" + WideFromNarrow(std::strerror(err)) + L")", kEvdevIoctlFailed);
        }
        return false;
    }
    // A gamepad reports the stick axes (ABS_X / ABS_Y at least) or gamepad
    // buttons.  Anything else — keyboard, mouse, power button, touchpad —
    // is closed again silently: it is not a fault.
    const bool hasSticks = EvdevBit(absBits, ABS_X) && EvdevBit(absBits, ABS_Y);
    const bool hasPadButtons = EvdevCountBits(keyBits, BTN_JOYSTICK, 0x13F) > 0;
    if (!hasSticks && !hasPadButtons) {
        ::close(fd);
        return false;
    }
    // It is a gamepad: fill the slot and its PadInfo.
    EvdevSlot& s = m_evdev[static_cast<size_t>(slot)];
    s.fd = fd;
    s.path = path;
    PadInfo& pi = m_padInfo[static_cast<size_t>(slot)];
    pi.slot = slot;
    pi.vendorId = id.vendor;
    pi.productId = id.product;
    pi.name = std::wstring(name, name + std::strlen(name));
    pi.numButtons = EvdevCountBits(keyBits, BTN_JOYSTICK, 0x13F);
    pi.hasZ = EvdevBit(absBits, ABS_Z);
    pi.hasR = EvdevBit(absBits, ABS_RZ);
    pi.hasU = EvdevBit(absBits, ABS_RX);
    pi.hasV = EvdevBit(absBits, ABS_RY);
    pi.hasPov = EvdevBit(absBits, ABS_HAT0X) && EvdevBit(absBits, ABS_HAT0Y);
    pi.type = ClassifyPad(pi.vendorId, pi.productId, pi.name, static_cast<UINT>(pi.numButtons), pi.hasPov, pi.typeName);
    pi.triggersOnZ = (pi.type == PadType::Xbox360 || pi.type == PadType::XboxOne || pi.type == PadType::Generic) && pi.hasZ;
    // On a triggersOnZ pad the right trigger is ABS_RZ, not a rotation axis:
    // winmm reports no R axis for such a pad, so hasR is false and
    // getNormalizedAxes will not read the trigger as rotation.
    if (pi.triggersOnZ) pi.hasR = false;
    // Axis count, as winmm would report it: the sticks, plus the trigger
    // axis (one combined axis for a triggersOnZ pad, two separate ones
    // otherwise).  The hat is the POV, not an axis.
    pi.numAxes = (EvdevBit(absBits, ABS_X) ? 1 : 0) + (EvdevBit(absBits, ABS_Y) ? 1 : 0)
        + (EvdevBit(absBits, ABS_RX) ? 1 : 0) + (EvdevBit(absBits, ABS_RY) ? 1 : 0)
        + (pi.triggersOnZ ? 1 : ((pi.hasZ ? 1 : 0) + (pi.hasR ? 1 : 0)));
    // Initial axis values and ranges, straight from the device.
    for (int i = 0; i < 6; ++i) {
        const int code = EvdevAxisCode(i);
        if (!EvdevBit(absBits, code)) continue;
        struct input_absinfo absinfo = {};
        if (::ioctl(fd, EVIOCGABS(code), &absinfo) < 0) continue;                  // a single bad axis does not kill the pad
        s.axisPresent[static_cast<size_t>(i)] = true;
        s.axisValue[static_cast<size_t>(i)] = absinfo.value;
        s.axisMin[static_cast<size_t>(i)] = absinfo.minimum;
        s.axisMax[static_cast<size_t>(i)] = absinfo.maximum;
    }
    if (EvdevBit(absBits, ABS_HAT0X)) {
        struct input_absinfo absinfo = {};
        if (::ioctl(fd, EVIOCGABS(ABS_HAT0X), &absinfo) == 0) s.hatX = std::clamp(absinfo.value, -1, 1);
    }
    if (EvdevBit(absBits, ABS_HAT0Y)) {
        struct input_absinfo absinfo = {};
        if (::ioctl(fd, EVIOCGABS(ABS_HAT0Y), &absinfo) == 0) s.hatY = std::clamp(absinfo.value, -1, 1);
    }
    if (EvdevBit(absBits, ABS_BRAKE)) {
        struct input_absinfo absinfo = {};
        if (::ioctl(fd, EVIOCGABS(ABS_BRAKE), &absinfo) == 0) {
            s.brakeMin = absinfo.minimum; s.brakeMax = absinfo.maximum;
            s.brake = EvdevAxis01(absinfo.value, absinfo.minimum, absinfo.maximum);
        }
    }
    if (EvdevBit(absBits, ABS_GAS)) {
        struct input_absinfo absinfo = {};
        if (::ioctl(fd, EVIOCGABS(ABS_GAS), &absinfo) == 0) {
            s.gasMin = absinfo.minimum; s.gasMax = absinfo.maximum;
            s.gas = EvdevAxis01(absinfo.value, absinfo.minimum, absinfo.maximum);
        }
    }
    // Initial button state: which gamepad buttons are already held down.
    unsigned char pressedBits[(KEY_MAX + 7) / 8] = {};
    if (::ioctl(fd, EVIOCGKEY(sizeof(pressedBits)), pressedBits) == 0) {
        for (int code = BTN_JOYSTICK; code <= 0x13F; ++code) {
            if (EvdevBit(pressedBits, code)) s.buttonMask |= (1u << (code - BTN_JOYSTICK));
        }
    }
    // The device opened cleanly: a later failure on this path may warn again.
    m_evdevWarnedPaths.erase(path);
    return true;
}

void Joystick::EvdevCloseSlot(int slot)
{
    EvdevSlot& s = m_evdev[static_cast<size_t>(slot)];
    if (s.fd >= 0) ::close(s.fd);
    s = EvdevSlot{};                                                              // fd back to -1, path and cached state cleared
    m_padInfo[static_cast<size_t>(slot)] = PadInfo{};                            // slot -1: no pad here
}

bool Joystick::EvdevProbeSlot(int slot)
{
    EvdevSlot& s = m_evdev[static_cast<size_t>(slot)];
    if (s.fd < 0) return false;
    // A device that is still there answers its name; a device that was
    // unplugged fails the query (ENODEV).  Any failure means the slot is dead.
    char name[256] = {};
    return ::ioctl(s.fd, EVIOCGNAME(sizeof(name)), name) >= 0;
}

bool Joystick::EvdevDrainSlot(int slot)
{
    EvdevSlot& s = m_evdev[static_cast<size_t>(slot)];
    if (s.fd < 0) return false;
    // Read all pending events.  The fd is non-blocking, so read() returns
    // -1/EAGAIN when the queue is empty; a read error other than EAGAIN
    // (usually ENODEV) or an EOF means the device is gone.
    struct input_event events[32];
    for (;;) {
        const ssize_t n = ::read(s.fd, events, sizeof(events));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;                                                 // EOF: device gone
        const size_t count = static_cast<size_t>(n) / sizeof(struct input_event);
        for (size_t i = 0; i < count; ++i) {
            const struct input_event& ev = events[i];
            if (ev.type == EV_ABS) {
                switch (ev.code) {
                case ABS_X:  s.axisValue[kEvdevAxisX] = ev.value; break;
                case ABS_Y:  s.axisValue[kEvdevAxisY] = ev.value; break;
                case ABS_Z:  s.axisValue[kEvdevAxisZ] = ev.value; break;
                case ABS_RZ: s.axisValue[kEvdevAxisR] = ev.value; break;
                case ABS_RX: s.axisValue[kEvdevAxisU] = ev.value; break;
                case ABS_RY: s.axisValue[kEvdevAxisV] = ev.value; break;
                case ABS_HAT0X: s.hatX = std::clamp(ev.value, -1, 1); break;
                case ABS_HAT0Y: s.hatY = std::clamp(ev.value, -1, 1); break;
                case ABS_BRAKE: s.brake = EvdevAxis01(ev.value, s.brakeMin, s.brakeMax); break;
                case ABS_GAS:   s.gas = EvdevAxis01(ev.value, s.gasMin, s.gasMax); break;
                default: break;
                }
            }
            else if (ev.type == EV_KEY && ev.code >= BTN_JOYSTICK && ev.code <= 0x13F) {
                const std::uint32_t bit = 1u << (ev.code - BTN_JOYSTICK);
                if (ev.value != 0) s.buttonMask |= bit; else s.buttonMask &= ~bit;
            }
            // EV_SYN and everything else: ignored.
        }
        if (static_cast<size_t>(n) < sizeof(events)) return true;                // partial read: the queue is empty now
    }
}

void Joystick::EvdevFillJoyInfo(int slot, JOYINFOEX& ji) const
{
    const EvdevSlot& s = m_evdev[static_cast<size_t>(slot)];
    const PadInfo& pi = m_padInfo[static_cast<size_t>(slot)];
    ji = {};
    ji.dwSize = sizeof(JOYINFOEX);
    ji.dwFlags = JOY_RETURNALL;
    // One axis: scale the raw evdev value through the range the device
    // reports into the winmm [0, 65535] range.  An axis the pad does not
    // have reads as the centre, so getNormalizedAxes never sees a false
    // full deflection from a missing axis.
    auto scale = [](int value, int minimum, int maximum) -> DWORD {
        if (maximum <= minimum) return 32767;
        const long long clamped = std::clamp<long long>(value, minimum, maximum);
        return static_cast<DWORD>((clamped - minimum) * 65535LL / (maximum - minimum));
    };
    ji.dwXpos = s.axisPresent[kEvdevAxisX] ? scale(s.axisValue[kEvdevAxisX], s.axisMin[kEvdevAxisX], s.axisMax[kEvdevAxisX]) : 32767;
    ji.dwYpos = s.axisPresent[kEvdevAxisY] ? scale(s.axisValue[kEvdevAxisY], s.axisMin[kEvdevAxisY], s.axisMax[kEvdevAxisY]) : 32767;
    if (pi.triggersOnZ) {
        // Triggers on the Z axis (Xbox-style): evdev reports the two
        // triggers as separate axes — ABS_Z is the left trigger and ABS_RZ
        // is the right trigger, each 0 at rest to 1 fully pressed — while
        // winmm combines them into one axis around the centre (left pushes
        // it up, right pulls it down; see the header comment).  Combine
        // them here so the game sees the same Z it sees on Windows, and so
        // a pad at rest reads as the centre instead of a fully pressed
        // trigger.  dwRpos is the centre: there is no R rotation axis on
        // such a pad (see EvdevOpenSlot).
        const float lt = s.axisPresent[kEvdevAxisZ] ? EvdevAxis01(s.axisValue[kEvdevAxisZ], s.axisMin[kEvdevAxisZ], s.axisMax[kEvdevAxisZ]) : 0.0f;
        const float rt = s.axisPresent[kEvdevAxisR] ? EvdevAxis01(s.axisValue[kEvdevAxisR], s.axisMin[kEvdevAxisR], s.axisMax[kEvdevAxisR]) : 0.0f;
        ji.dwZpos = static_cast<DWORD>(std::clamp(32767.5f + (lt - rt) * 32767.5f, 0.0f, 65535.0f));
        ji.dwRpos = 32767;
    }
    else {
        ji.dwZpos = s.axisPresent[kEvdevAxisZ] ? scale(s.axisValue[kEvdevAxisZ], s.axisMin[kEvdevAxisZ], s.axisMax[kEvdevAxisZ]) : 32767;
        ji.dwRpos = s.axisPresent[kEvdevAxisR] ? scale(s.axisValue[kEvdevAxisR], s.axisMin[kEvdevAxisR], s.axisMax[kEvdevAxisR]) : 32767;
    }
    ji.dwUpos = s.axisPresent[kEvdevAxisU] ? scale(s.axisValue[kEvdevAxisU], s.axisMin[kEvdevAxisU], s.axisMax[kEvdevAxisU]) : 32767;
    ji.dwVpos = s.axisPresent[kEvdevAxisV] ? scale(s.axisValue[kEvdevAxisV], s.axisMin[kEvdevAxisV], s.axisMax[kEvdevAxisV]) : 32767;
    // Buttons: translate the evdev button mask into the raw winmm numbering
    // for this pad's family, using the same PadMap as ReadPadButtons, so a
    // game sees the same raw buttons on Linux as it does on Windows.
    const PadMap* m = PadMapFor(pi.type);
    auto raw = [](int n) { return n > 0 ? (1u << (n - 1)) : 0u; };
    auto evDown = [&s](int code) { return (s.buttonMask & (1u << (code - BTN_JOYSTICK))) != 0; };
    DWORD buttons = 0;
    if (evDown(BTN_SOUTH))  buttons |= raw(m->south);
    if (evDown(BTN_EAST))   buttons |= raw(m->east);
    if (evDown(BTN_WEST))   buttons |= raw(m->west);
    if (evDown(BTN_NORTH))  buttons |= raw(m->north);
    if (evDown(BTN_TL))     buttons |= raw(m->l1);
    if (evDown(BTN_TR))     buttons |= raw(m->r1);
    if (evDown(BTN_TL2))    buttons |= raw(m->l2);
    if (evDown(BTN_TR2))    buttons |= raw(m->r2);
    if (evDown(BTN_SELECT)) buttons |= raw(m->select);
    if (evDown(BTN_START))  buttons |= raw(m->start);
    if (evDown(BTN_THUMBL)) buttons |= raw(m->l3);
    if (evDown(BTN_THUMBR)) buttons |= raw(m->r3);
    if (evDown(BTN_MODE))   buttons |= raw(m->guide);
    // Separate trigger axes (ABS_BRAKE / ABS_GAS, some drivers): past 35 %
    // counts as the digital L2 / R2, mirroring the Xbox Z-trigger rule in
    // ReadPadButtons.  (The analog values themselves are not in JOYINFOEX —
    // winmm has no per-trigger analog fields either — so the digital bit is
    // what the game sees, on both platforms.)
    if (s.brake > 0.35f) buttons |= raw(m->l2);
    if (s.gas > 0.35f) buttons |= raw(m->r2);
    ji.dwButtons = buttons;
    unsigned buttonCount = 0;
    for (DWORD b = buttons; b != 0; b >>= 1) buttonCount += (b & 1u);
    ji.dwButtonNumber = buttonCount;
    ji.dwPOV = pi.hasPov ? EvdevHatToPov(s.hatX, s.hatY) : JOY_POVCENTERED;
}
#endif

#if defined(PLATFORM_WINDOWS)
bool Joystick::readJoystickState(int joystickID, JoystickState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    if (std::find(activeJoysticks.begin(), activeJoysticks.end(), joystickID) == activeJoysticks.end()) {
        LogJoystickWarning(L"Invalid joystick ID: " + std::to_wstring(joystickID) + L" (not in the active list, active count " + std::to_wstring(activeJoysticks.size()) + L"), returning zeroed state");
        return false;
    }

    state.joystickID = joystickID;
    state.info.dwSize = sizeof(JOYINFOEX);
    state.info.dwFlags = JOY_RETURNALL;

    const MMRESULT result = joyGetPosEx(static_cast<UINT>(joystickID), &state.info);
    if (result != JOYERR_NOERROR) {
        LogJoystickWarningWithResult(L"Failed to read joystick " + std::to_wstring(joystickID), result);
        return false;
    }
    return true;
}
#elif defined(PLATFORM_LINUX)
bool Joystick::readJoystickState(int joystickID, JoystickState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    if (std::find(activeJoysticks.begin(), activeJoysticks.end(), joystickID) == activeJoysticks.end()) {
        LogJoystickWarning(L"Invalid joystick ID: " + std::to_wstring(joystickID) + L" (not in the active list, active count " + std::to_wstring(activeJoysticks.size()) + L"), returning zeroed state");
        return false;
    }
    state.joystickID = joystickID;
    // ReadRawPad drains the evdev queue and fills the state; a read failure
    // is warned about there (hard point, status snapshot).
    return ReadRawPad(joystickID, state.info);
}
#else
bool Joystick::readJoystickState(int joystickID, JoystickState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    state.joystickID = joystickID;
    state.info = {};
    // Android / macOS / iOS: no joystick backend yet.  Deliberately silent
    // (disabled, not stubbed).
    return false;
}
#endif

bool Joystick::loadMapping(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        LogJoystickWarning(L"Failed to open mapping file: " + std::wstring(filename.begin(), filename.end()) + L" (using defaults, no mapping loaded)");
        return false;
    }

    try {
        nlohmann::json jsonData;
        file >> jsonData;

        if (!jsonData.is_object()) {
            LogJoystickWarning(L"Error parsing mapping file: " + std::wstring(filename.begin(), filename.end()) + L" (top level is not a JSON object, keeping existing mappings)");
            return false;
        }

        // Parse into a temporary map first.  The live map is only replaced
        // once parsing has succeeded, so a bad entry or a bad file can never
        // wipe the mappings that were already working.
        std::unordered_map<int, ButtonMapping> newMappings;
        for (auto& [joystickIDStr, mapping] : jsonData.items()) {
            int id = 0;
            try {
                id = std::stoi(joystickIDStr);
            }
            catch (const std::exception& e) {
                LogJoystickWarning(L"Error parsing mapping file: skipping joystick entry \"" + std::wstring(joystickIDStr.begin(), joystickIDStr.end()) + L"\" (" + WideFromNarrow(e.what()) + L")");
                continue;
            }
            if (id < 0 || id >= MAX_JOYSTICKS) {
                LogJoystickWarning(L"Error parsing mapping file: skipping joystick ID " + std::to_wstring(id) + L", expected 0.." + std::to_wstring(MAX_JOYSTICKS - 1));
                continue;
            }
            if (!mapping.is_object()) {
                LogJoystickWarning(L"Error parsing mapping file: skipping joystick " + std::to_wstring(id) + L", its mapping is not a JSON object");
                continue;
            }
            for (auto& [buttonStr, key] : mapping.items()) {
                int button = 0;
                try {
                    button = std::stoi(buttonStr);
                }
                catch (const std::exception& e) {
                    LogJoystickWarning(L"Error parsing mapping file: skipping button \"" + std::wstring(buttonStr.begin(), buttonStr.end()) + L"\" for joystick " + std::to_wstring(id) + L" (" + WideFromNarrow(e.what()) + L")");
                    continue;
                }
                if (button < 0 || button > 31) {
                    LogJoystickWarning(L"Error parsing mapping file: skipping button " + std::to_wstring(button) + L" for joystick " + std::to_wstring(id) + L", expected 0..31");
                    continue;
                }
                if (!key.is_number_integer() && !key.is_number_unsigned()) {
                    LogJoystickWarning(L"Error parsing mapping file: skipping button " + std::to_wstring(button) + L" for joystick " + std::to_wstring(id) + L", its key is not a number");
                    continue;
                }
                int vkCode = 0;
                try {
                    vkCode = key.get<int>();
                }
                catch (const std::exception& e) {
                    LogJoystickWarning(L"Error parsing mapping file: skipping button " + std::to_wstring(button) + L" for joystick " + std::to_wstring(id) + L" (" + WideFromNarrow(e.what()) + L")");
                    continue;
                }
                if (vkCode < 0 || vkCode > 255) {
                    LogJoystickWarning(L"Error parsing mapping file: skipping key " + std::to_wstring(vkCode) + L" for button " + std::to_wstring(button) + L", expected 0..255");
                    continue;
                }
                newMappings[id].buttonToKey[button] = vkCode;
            }
        }
        joystickMappings.swap(newMappings);
        #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick mapping loaded successfully.");
        #endif
        return true;
    }
    catch (const std::exception& e) {
        LogJoystickWarning(L"Error parsing mapping file: " + WideFromNarrow(e.what()) + L" (keeping existing mappings)");
        return false;
    }
    catch (...) {
        LogJoystickWarning(L"Error parsing mapping file: unknown failure (keeping existing mappings)");
        return false;
    }
}

bool Joystick::saveMapping(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        LogJoystickWarning(L"Failed to save mapping file: " + std::wstring(filename.begin(), filename.end()) + L" (mappings kept in memory)");
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
        file.flush();
        if (!file.good()) {
            LogJoystickWarning(L"Failed to save mapping file: " + std::wstring(filename.begin(), filename.end()) + L" (write or flush failed, file may be incomplete)");
            return false;
        }
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick mapping saved successfully.");
        return true;
    }
    catch (const std::exception& e) {
        LogJoystickWarning(L"Error writing mapping file: " + WideFromNarrow(e.what()));
        return false;
    }
    catch (...) {
        LogJoystickWarning(L"Error writing mapping file: unknown failure");
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

        const auto mappingIt = joystickMappings.find(joystickID);
        if (mappingIt == joystickMappings.end()) continue;
        for (const auto& [button, vkKey] : mappingIt->second.buttonToKey) {
            if (button < 0 || button > 31) {
                LogJoystickWarning(L"Skipping invalid button mapping for joystick " + std::to_wstring(joystickID) + L": button " + std::to_wstring(button) + L" is outside 0..31");
                continue;
            }
            if (vkKey < 0 || vkKey > 255) {
                LogJoystickWarning(L"Skipping invalid button mapping for joystick " + std::to_wstring(joystickID) + L": key " + std::to_wstring(vkKey) + L" is outside 0..255");
                continue;
            }
            if (state.info.dwButtons & (1u << static_cast<unsigned>(button))) {
                postKeyEvent(vkKey);
            }
        }
    }
}

#if defined(PLATFORM_WINDOWS)
void Joystick::postKeyEvent(int vkCode) {
    if (vkCode < 0 || vkCode > 255) {
        LogJoystickWarning(L"postKeyEvent: invalid virtual-key code " + std::to_wstring(vkCode) + L", expected 0..255, key event ignored");
        return;
    }
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vkCode);
    // KEYUP only, unchanged from the original behaviour: this posts a key
    // release, not a press.  Only the result check below is new.
    input.ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sent = SendInput(1, &input, sizeof(INPUT));
    if (sent == 0) {
        const DWORD lastError = GetLastError();
        LogJoystickWarning(L"postKeyEvent: SendInput failed for key " + std::to_wstring(vkCode) + L" (GetLastError=" + std::to_wstring(lastError) + L"), key event dropped");
        return;
    }
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Posted WM_KEYUP event for key: " + std::to_wstring(vkCode));
    #endif
}
#elif defined(PLATFORM_LINUX)
void Joystick::postKeyEvent(int vkCode) {
    if (vkCode < 0 || vkCode > 255) {
        LogJoystickWarning(L"postKeyEvent: invalid virtual-key code " + std::to_wstring(vkCode) + L", expected 0..255, key event ignored");
        return;
    }
    // Linux: there is no SendInput equivalent in the engine, and libXtst
    // (XTest key injection) is not a build dependency, so a button-to-key
    // mapping cannot post a key event.  The event is dropped.  This is a
    // limitation of the key-injection path only — the pad itself still works
    // through ReadRawPad / processJoystickMovement — and the warning is
    // throttled by the diagnostics layer, so it cannot flood the log.
    // (Button-to-key mappings are not loaded by default — loadMapping is
    // commented out in the constructor — so this path is rarely reached.)
    LogJoystickWarning(L"postKeyEvent: key injection is not available on Linux (no XTest backend), key event " + std::to_wstring(vkCode) + L" dropped");
}
#else
void Joystick::postKeyEvent(int vkCode) {
    if (vkCode < 0 || vkCode > 255) {
        LogJoystickWarning(L"postKeyEvent: invalid virtual-key code " + std::to_wstring(vkCode) + L", expected 0..255, key event ignored");
        return;
    }
    // Android / macOS / iOS: no key-injection backend yet.  Deliberately
    // silent (disabled, not stubbed).
}
#endif

//---------------------------------------------------------------------------------
// New Joystick Movement Functions
//---------------------------------------------------------------------------------

float Joystick::normalizeJoystickAxis(DWORD axisValue) {
    // Convert from the raw joystick value [0, 65535] to normalized [-1, 1]
    // with a deadzone in the center.  A driver value outside the documented
    // range is clamped first, and the result is clamped last, so the return
    // value can never leave [-1, 1] however bad the input is.
    const DWORD clampedValue = (axisValue > 65535u) ? 65535u : axisValue;
    float normalized = (static_cast<float>(clampedValue) - JOYSTICK_CENTER) / JOYSTICK_CENTER;

    // Apply deadzone
    if (std::abs(normalized) < JOYSTICK_DEADZONE / JOYSTICK_CENTER) {
        return 0.0f;
    }

    // Adjust range to account for deadzone
    float result = 0.0f;
    if (normalized > 0) {
        result = (normalized - JOYSTICK_DEADZONE / JOYSTICK_CENTER) /
            (1.0f - JOYSTICK_DEADZONE / JOYSTICK_CENTER);
    }
    else {
        result = (normalized + JOYSTICK_DEADZONE / JOYSTICK_CENTER) /
            (1.0f - JOYSTICK_DEADZONE / JOYSTICK_CENTER);
    }
    return std::clamp(result, -1.0f, 1.0f);
}

JoystickAxes Joystick::getNormalizedAxes(int joystickID) {
    JoystickAxes axes = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    JoystickState state;

    if (!readJoystickState(joystickID, state)) {
        return axes;
    }

    // Which rotation axes exist comes from the pad's capabilities, not from
    // dwFlags: dwFlags was set to JOY_RETURNALL by the caller, so testing it
    // here would claim every rotation axis exists on every pad.
    const PadInfo pad = GetPadInfo(joystickID);

    // Normalize all axes
    axes.x = normalizeJoystickAxis(state.info.dwXpos);

    // Y-axis is inverted in most joysticks to match the screen coordinate
    // system (down is positive), but for cameras we want up to be positive
    axes.y = -normalizeJoystickAxis(state.info.dwYpos);

    // Z axis (often used as a throttle or trigger), only when the pad has one;
    // otherwise the axis stays at 0 instead of reporting a false full deflection.
    axes.z = pad.hasZ ? normalizeJoystickAxis(state.info.dwZpos) : 0.0f;

    // Rotation axes if available
    if (pad.hasR) {
        axes.rx = normalizeJoystickAxis(state.info.dwRpos);
    }

    if (pad.hasU) {
        axes.ry = normalizeJoystickAxis(state.info.dwUpos);
    }

    if (pad.hasV) {
        axes.rz = normalizeJoystickAxis(state.info.dwVpos);
    }

    return axes;
}

void Joystick::processJoystickMovement(int joystickID) {
    {
        std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
        if (std::find(activeJoysticks.begin(), activeJoysticks.end(), joystickID) == activeJoysticks.end()) {
            LogJoystickWarning(L"processJoystickMovement: invalid joystick ID " + std::to_wstring(joystickID) + L" (not active, or just unplugged), movement ignored");
            return;
        }
    }

    JoystickAxes axes = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    try {
        axes = getNormalizedAxes(joystickID);

        // Process movement based on current mode
        if (m_movementMode == MovementMode::MODE_3D) {
            if (m_camera != nullptr) {
                process3DMovement(axes);
            }
            else {
                LogJoystickWarning(L"processJoystickMovement: 3D mode requested but no camera is set, falling back to 2D movement");
                process2DMovement(axes);
            }
        }
        else {
            process2DMovement(axes);
        }
    }
    catch (const std::exception& e) {
        LogJoystickWarning(L"processJoystickMovement: movement failed for joystick " + std::to_wstring(joystickID) + L" (" + WideFromNarrow(e.what()) + L"), movement ignored");
        return;
    }
    catch (...) {
        LogJoystickWarning(L"processJoystickMovement: movement failed for joystick " + std::to_wstring(joystickID) + L" (unknown failure), movement ignored");
        return;
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
        LogJoystickWarning(L"process3DMovement: no camera set, 3D movement ignored");
        return;
    }
    if (!std::isfinite(axes.x) || !std::isfinite(axes.y) || !std::isfinite(axes.z) ||
        !std::isfinite(axes.rx) || !std::isfinite(axes.ry)) {
        LogJoystickWarning(L"process3DMovement: non-finite axes, 3D movement ignored");
        return;
    }

    try {
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
    catch (const std::exception& e) {
        LogJoystickWarning(L"process3DMovement: camera movement failed (" + WideFromNarrow(e.what()) + L"), movement ignored");
        return;
    }
    catch (...) {
        LogJoystickWarning(L"process3DMovement: camera movement failed (unknown failure), movement ignored");
        return;
    }
}

void Joystick::process2DMovement(const JoystickAxes& axes) {
    // In 2D mode, we simply update the internal 2D position
    // based on joystick input
    if (!std::isfinite(axes.x) || !std::isfinite(axes.y)) {
        LogJoystickWarning(L"process2DMovement: non-finite axes, 2D movement ignored");
        return;
    }
    if (!std::isfinite(m_movementSensitivity)) {
        LogJoystickWarning(L"process2DMovement: movement sensitivity is not finite, 2D movement ignored");
        return;
    }

    // Update position based on normalized axes and sensitivity
    m_last2DPosition.x += axes.x * m_movementSensitivity;
    m_last2DPosition.y += axes.y * m_movementSensitivity;

    // Optional: Clamp values to a specific range if needed for your 2D environment
    // m_last2DPosition.x = std::max(0.0f, std::min(screenWidth, m_last2DPosition.x));
    // m_last2DPosition.y = std::max(0.0f, std::min(screenHeight, m_last2DPosition.y));
}

// Clamp one sensitivity value and warn when it had to be repaired.  A
// non-finite or non-positive value uses the default; a value outside the
// range is clamped to the nearest end of the range.  Implemented as a
// lambda inside each Configure function so it can use the member warning
// helper directly; there is no free-function version.

// Example function for 3D movement with camera
void Joystick::ConfigureFor3DMovement() 
{
    // Set movement mode to 3D
    setMovementMode(MovementMode::MODE_3D);

    // Apply sensitivity values from configuration, repaired when invalid:
    // a non-finite or non-positive value falls back to the default, a value
    // outside the range is clamped, and either repair is warned about.
    auto repairSensitivity = [&](float value, float minimum, float maximum, float fallback, const wchar_t* name) -> float {
        if (!std::isfinite(value) || value <= 0.0f) {
            LogJoystickWarning(std::wstring(L"ConfigureFor3DMovement: invalid ") + name + L" sensitivity, using default");
            return fallback;
        }
        if (value < minimum || value > maximum) {
            LogJoystickWarning(std::wstring(L"ConfigureFor3DMovement: ") + name + L" sensitivity " + std::to_wstring(value) + L" is outside the range, clamping it");
            return std::clamp(value, minimum, maximum);
        }
        return value;
    };
    setMovementSensitivity(repairSensitivity(static_cast<float>(config.myConfig.joystickSensitivity), kJoystickMoveSensitivityMin, kJoystickMoveSensitivityMax, kJoystickMoveSensitivityDefault, L"movement"));
    setRotationSensitivity(repairSensitivity(static_cast<float>(config.myConfig.joystickRotationSensitivity), kJoystickRotSensitivityMin, kJoystickRotSensitivityMax, kJoystickRotSensitivityDefault, L"rotation"));

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

    // Apply movement sensitivity from configuration, repaired when invalid.
    const float configured = static_cast<float>(config.myConfig.joystickSensitivity);
    if (!std::isfinite(configured) || configured <= 0.0f) {
        LogJoystickWarning(L"ConfigureFor2DMovement: invalid movement sensitivity, using default");
        setMovementSensitivity(kJoystickMoveSensitivityDefault);
    }
    else if (configured < kJoystickMoveSensitivityMin || configured > kJoystickMoveSensitivityMax) {
        LogJoystickWarning(L"ConfigureFor2DMovement: movement sensitivity is outside the range, clamping it");
        setMovementSensitivity(std::clamp(configured, kJoystickMoveSensitivityMin, kJoystickMoveSensitivityMax));
    }
    else {
        setMovementSensitivity(configured);
    }

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

#if defined(PLATFORM_WINDOWS)
#pragma warning(pop)
#endif
