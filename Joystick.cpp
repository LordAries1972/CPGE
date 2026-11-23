#include "Includes.h"
#include "Joystick.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>

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
#else
std::wstring Joystick::DescribeWinMmResult(MMRESULT result)
{
    // No winmm on Linux/Android: there is no winmm result to describe.
    std::wostringstream out;
    out << L"0x" << std::hex << std::uppercase << static_cast<unsigned int>(result) << std::dec << L" (no winmm result on Linux)";
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
#else
bool Joystick::IsHardPointResult(MMRESULT result)
{
    // No winmm results exist on Linux/Android, so no result is a hard point.
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

#if !defined(PLATFORM_WINDOWS)
// Linux/Android stub (Phase A): one-time warning that joystick input is
// unavailable on Linux in this phase.  The message is also stored as the
// last warning so GetLastJoystickWarning() / DumpJoystickStatus() report it.
void Joystick::LogLinuxStubWarningOnce()
{
    static bool warned = false;
    if (warned) return;
    warned = true;
    std::wstring warning;
    {
        std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
        m_lastWarning = L"Joystick input is unavailable on Linux in this phase (no evdev backend yet); keyboard input is used instead";
        warning = m_lastWarning;
    }
    Debug::logDiagLevelMessage(LogLevel::LOG_WARNING, L"[Joystick] " + warning);
}
#endif

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

//    saveMapping();                                            // Not Required for the moment, so we will NOT use, please leave this line here.
    #if defined(_DEBUG) && defined(_DUBUG_JOYSTICK_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Joystick Class Destroyed...");
    #endif

    isDestroyed = true;
}

// ---- pad identification ------------------------------------------------------------------------------------------------------------------------------------------
#if defined(PLATFORM_WINDOWS)
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
#endif
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
#if defined(PLATFORM_WINDOWS)
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
#endif
#if defined(PLATFORM_WINDOWS)
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
#else
uint32_t Joystick::ReadPadButtons(int slot, const JOYINFOEX& ji, float* leftTrigger, float* rightTrigger)
{
    (void)slot;
    (void)ji;
    if (leftTrigger) *leftTrigger = 0.0f;
    if (rightTrigger) *rightTrigger = 0.0f;
    LogLinuxStubWarningOnce();
    return 0;
}
#endif

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
#else
void Joystick::detectJoysticks() {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    // Linux/Android stub (Phase A): no evdev backend yet, so no pad is ever
    // found.  Clear any previous state and report zero active controllers.
    activeJoysticks.clear();
    m_padInfo = std::array<PadInfo, MAX_JOYSTICKS>{};
    m_activeCount.store(0, std::memory_order_relaxed);
    LogLinuxStubWarningOnce();
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
#else
bool Joystick::ReadRawPad(int joystickID, JOYINFOEX& ji) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    (void)joystickID;
    ji = {};
    LogLinuxStubWarningOnce();
    return false;
}
#endif

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
#else
bool Joystick::readJoystickState(int joystickID, JoystickState& state) {
    std::lock_guard<std::recursive_mutex> lock(m_joyMutex);
    state.joystickID = joystickID;
    state.info = {};
    LogLinuxStubWarningOnce();
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
#else
void Joystick::postKeyEvent(int vkCode) {
    if (vkCode < 0 || vkCode > 255) {
        LogJoystickWarning(L"postKeyEvent: invalid virtual-key code " + std::to_wstring(vkCode) + L", expected 0..255, key event ignored");
        return;
    }
    // Linux/Android stub (Phase A): no SendInput backend, so the key event is dropped.
    LogLinuxStubWarningOnce();
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
