
#include "Includes.h"
#include <shellapi.h>
#include <thread>

#if defined(__USE_OPENGL__)
#include "OpenGLFXManager.h"
#elif defined(__USE_VULKAN__)
#include "VULKAN_FXManager.h"
#else
#include "DX_FXManager.h"
#endif

#include "ThreadManager.h"
#include "SoundManager.h"
#include "GUIManager.h"
#include "WinSystem.h"
#include "Debug.h"
#include "Configuration.h"

extern std::shared_ptr<Renderer> renderer;
extern SoundManager soundManager;
#if defined(__USE_OPENGL__)
extern GLFXManager fxManager;
#elif defined(__USE_VULKAN__)
extern VKFXManager fxManager;
#else
extern FXManager fxManager;
#endif
extern ThreadManager threadManager;

// Forward-declared in main.cpp (no longer static)
#if defined(PLATFORM_WINDOWS)
void ApplySystemMasterVolume(int vol64);
#endif

// ---------------------------------------------------------------------------
// Display mode enumeration helpers
// ---------------------------------------------------------------------------
struct DispMode { int w, h, hz; };

static std::vector<DispMode> ScanDisplayModes()
{
    std::vector<DispMode> out;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
        if (dm.dmBitsPerPel != 32) continue;
        DispMode m{ (int)dm.dmPelsWidth, (int)dm.dmPelsHeight, (int)dm.dmDisplayFrequency };
        bool dup = false;
        for (auto& e : out) if (e.w == m.w && e.h == m.h && e.hz == m.hz) { dup = true; break; }
        if (!dup) out.push_back(m);
    }
    // Sort: largest resolution first, then highest refresh rate
    std::sort(out.begin(), out.end(), [](const DispMode& a, const DispMode& b) {
        if (a.w != b.w) return a.w > b.w;
        if (a.h != b.h) return a.h > b.h;
        return a.hz > b.hz;
    });
    return out;
}

static std::vector<std::pair<int,int>> UniqueResolutions(const std::vector<DispMode>& modes)
{
    std::vector<std::pair<int,int>> res;
    for (auto& m : modes) {
        bool dup = false;
        for (auto& r : res) if (r.first == m.w && r.second == m.h) { dup = true; break; }
        if (!dup) res.push_back({ m.w, m.h });
    }
    return res;
}

static std::vector<int> RatesFor(const std::vector<DispMode>& modes, int w, int h)
{
    std::vector<int> rates;
    for (auto& m : modes) {
        if (m.w != w || m.h != h) continue;
        bool dup = false;
        for (int r : rates) if (r == m.hz) { dup = true; break; }
        if (!dup) rates.push_back(m.hz);
    }
    std::sort(rates.begin(), rates.end(), std::greater<int>());
    return rates;
}

// Display mode name strings — file scope so lambdas need no capture
static const wchar_t* const DISP_MODE_NAMES[3] = {
    L"Windowed", L"Borderless", L"Full Screen"
};

// Available-renderer list.
// Windows always shows all four so the user can select the preferred executable
// (DX / OpenGL / Vulkan) regardless of which backend is currently compiled in.
// Linux/Android: only compiled-in backends are shown; slider hidden when count == 1.
struct RendererEntry { int type; const wchar_t* name; };
#if defined(PLATFORM_WINDOWS)
static const RendererEntry AVAILABLE_RENDERERS[] = {
    { 0, L"DirectX 11" },
    { 1, L"DirectX 12" },
    { 2, L"OpenGL"     },
    { 3, L"Vulkan"     },
};
#else
static const RendererEntry AVAILABLE_RENDERERS[] = {
#  if defined(__USE_OPENGL__)
    { 0, L"OpenGL" },
#  endif
#  if defined(__USE_VULKAN__)
    { 1, L"Vulkan" },
#  endif
};
#endif
static constexpr int RENDERER_COUNT =
    (int)(sizeof(AVAILABLE_RENDERERS) / sizeof(AVAILABLE_RENDERERS[0]));

// ---------------------------------------------------------------------------
// Local formatting helpers
// ---------------------------------------------------------------------------
static std::wstring CfgFmtFloat(long double val, int dec = 4)
{
    wchar_t buf[64];
    swprintf_s(buf, 64, L"%.*f", dec, (double)val);
    return buf;
}
static std::wstring CfgFmtInt(int val) { return std::to_wstring(val); }

// ---------------------------------------------------------------------------
// CreateConfigWindow
//
// Architecture:
//   - revertCfg  : snapshot of config.myConfig taken at window-open time.
//                  Restored when the user presses Close or X (cancel).
//   - config.myConfig is modified DIRECTLY by all controls.
//   - Audio changes call config.applyLive() immediately (fires the callback
//     registered in main.cpp which pushes values to soundManager, xmPlayer,
//     ttsManager, screenRecorder, ApplySystemMasterVolume).
//   - Game Play changes update config.myConfig in-place; camera / game code
//     reads config.myConfig live so they take effect next frame.
//   - Video changes set needsVideoRestart = true.  On Save, a 10-second
//     restart-notification window is shown; the game restarts automatically
//     unless the user clicks "Cancel Restart".
//
// Layout (screen pixels, window centered on screen):
//   WW = 620, WH = 480  (centered horizontally and vertically)
//   TitleBar      : (WX, WY)          620 x 28  (full width — no close-X)
//   Tab buttons   : (WX, WY+28)       124 x 26 each, 5 tabs
//   Bevel content : (WX, WY+54)       620 x 370
//   Bottom buttons: (WX+10, WY+432)
//
// Control id prefixes:
//   tbn0..tbn4  -> tab nav buttons
//   t0_*        -> Game Play
//   t1_*        -> Audio
//   t2_*        -> Video
//   t3_*        -> Controls
//   t4_*        -> Key Mapping
// ---------------------------------------------------------------------------

void GUIManager::CreateConfigWindow()
{
    const std::string WIN_NAME = "ConfigWindow";

    if (GetWindow(WIN_NAME)) return;

    if (!myRenderer) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateConfigWindow - myRenderer is null");
        return;
    }

    const float WW = 620.0f, WH = 480.0f;
    const float WX = (static_cast<float>(myRenderer->iOrigWidth)  - WW) / 2.0f;
    const float WY = (static_cast<float>(myRenderer->iOrigHeight) - WH) / 2.0f;

    CreateMyWindow(WIN_NAME, GUIWindowType::Dialog,
        Vector2(WX, WY), Vector2(WW, WH),
        MyColor(0, 0, 0, 220),   // semi-opaque: was 128 (50% transparent), raised so 3D scene doesn't bleed through
        int(BlitObj2DIndexType::NONE));

    auto configWindow = GetWindow(WIN_NAME);
    if (!configWindow) return;

    configWindow->isModal = true;

    std::weak_ptr<GUIWindow> weakWin = configWindow;

    // -----------------------------------------------------------------------
    // Snapshot for revert-on-cancel
    // -----------------------------------------------------------------------
    auto revertCfg       = std::make_shared<MyConfig>(config.myConfig);
    auto needsVideoRestart = std::make_shared<bool>(false);
    auto actTab          = std::make_shared<int>(0);

    // -----------------------------------------------------------------------
    // Display mode data (Video tab)
    // -----------------------------------------------------------------------
    auto allModes  = std::make_shared<std::vector<DispMode>>(ScanDisplayModes());
    auto uniqueRes = std::make_shared<std::vector<std::pair<int,int>>>(UniqueResolutions(*allModes));

    // Find starting resolution index
    int startResIdx = 0;
    for (int i = 0; i < (int)uniqueRes->size(); ++i) {
        if ((*uniqueRes)[i].first  == config.myConfig.resolutionWidth &&
            (*uniqueRes)[i].second == config.myConfig.resolutionHeight) {
            startResIdx = i; break;
        }
    }
    auto resIdx = std::make_shared<int>(startResIdx);

    // Find starting refresh rate list + index
    auto startRates = uniqueRes->empty()
        ? std::vector<int>{}
        : RatesFor(*allModes, (*uniqueRes)[startResIdx].first, (*uniqueRes)[startResIdx].second);
    int startRateIdx = 0;
    for (int i = 0; i < (int)startRates.size(); ++i) {
        if (startRates[i] == config.myConfig.refreshRate) { startRateIdx = i; break; }
    }
    auto rateVec = std::make_shared<std::vector<int>>(startRates);
    auto rateIdx = std::make_shared<int>(startRateIdx);

    // -----------------------------------------------------------------------
    // Tab switching — show/hide t{n}_* controls, re-colour tab buttons
    // -----------------------------------------------------------------------
    auto doSwitchTab = [weakWin, actTab](int t) {
        *actTab = t;
        if (auto w = weakWin.lock()) {
            for (auto& c : w->controls) {
                if (c.id.size() >= 3 && c.id[0] == 't' &&
                    std::isdigit((unsigned char)c.id[1]) && c.id[2] == '_')
                    c.isVisible = (c.id[1] - '0' == t);

                if (c.id.size() == 4 && c.id[0] == 't' && c.id[1] == 'b' &&
                    c.id[2] == 'n' && std::isdigit((unsigned char)c.id[3]))
                    c.txtColor = (c.id[3] - '0' == t)
                        ? MyColor(255, 220, 0, 255)
                        : MyColor(150, 150, 150, 255);
            }
        }
    };

    // -----------------------------------------------------------------------
    // Control-builder lambdas
    // -----------------------------------------------------------------------
    // All row types share one label column width so every second column
    // (readout / toggle / info value) is left-aligned across ALL tabs.
    //
    // Column map (screen coords, window at x=25):
    //   Col 0  label    :  35 .. 235   (LBL_W  = 200)
    //   Col 1  readout  : 240 .. 325   (SLR_VAL_W = 85)
    //   Col 2  track    : 330 .. 715   (SLR_W  = 385)
    const float LBL_W     = 200.0f;   // label column — same for ALL row types
    const float SLR_VAL_W = 85.0f;    // readout column
    const float ROW_H     = 26.0f;
    const float CX        = WX + 10.0f;
    const float CONT_Y    = WY + TITLEBAR_HEIGHT + 26.0f;
    const float CY        = CONT_Y + 10.0f;
    const float ROW       = 32.0f;

    const float SLR_X = CX + LBL_W + 5.0f + SLR_VAL_W + 5.0f;  // 330
    const float SLR_W = (WX + WW - 10.0f) - SLR_X;              // 385

    auto VAL_X = [&]{ return CX + LBL_W + 5.0f; };  // 240

    auto addLabel = [&](const std::string& id, float x, float y, float w, float h,
                        const std::wstring& text, float fs = 13.0f, bool vis = true) {
        GUIControl c;
        c.type = GUIControlType::TextArea; c.id = id;
        c.position = Vector2(x, y);  c.size = Vector2(w, h);
        c.bgColor = MyColor(0, 0, 0, 0);
        c.hoverColor = MyColor(0, 0, 0, 0);
        c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        c.txtColor = MyColor(210, 210, 210, 255);
        c.label = text;  c.lblFontSize = fs;  c.isVisible = vis;
        configWindow->AddControl(c);
    };

    auto addButton = [&](const std::string& id, float x, float y, float w, float h,
                         const std::wstring& lbl, float fs, bool vis,
                         std::function<void()> onClick,
                         uint8_t bgAlpha = 255) {
        GUIControl c;
        c.type = GUIControlType::Button; c.id = id;
        c.position = Vector2(x, y);  c.size = Vector2(w, h);
        c.bgColor    = MyColor(20, 20, 35, bgAlpha);   // bgAlpha controls default-state opacity
        c.hoverColor = MyColor(60, 60, 90, 255);        // always fully solid on hover
        c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
        c.txtColor = MyColor(210, 210, 210, 255);
        c.label = lbl;  c.lblFontSize = fs;  c.isVisible = vis;
        c.onMouseBtnDown = onClick;
        configWindow->AddControl(c);
    };

    // label | [==O== ON/OFF toggle slider]
    auto addTogSlider = [&](const std::string& pfx, float y, const std::wstring& name,
                            bool initState, bool vis, int tabIdx,
                            std::function<void(bool)> onChange) {
        addLabel(pfx + "_lbl", CX, y, LBL_W, ROW_H, name, 13.0f, vis);
        GUIControl tc;
        tc.type             = GUIControlType::ToggleSlider;
        tc.id               = pfx + "_tog";
        tc.position         = Vector2(VAL_X(), y);
        tc.size             = Vector2(90.0f, ROW_H);
        tc.isVisible        = vis;
        tc.bgColor          = MyColor(0, 0, 0, 0);
        tc.hoverColor       = MyColor(0, 0, 0, 0);
        tc.bgTextureId      = int(BlitObj2DIndexType::NONE);
        tc.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        tc.sliderMin        = 0.0f;
        tc.sliderMax        = 1.0f;
        tc.sliderValue      = initState ? 1.0f : 0.0f;
        tc.onSliderChanged  = [actTab, tabIdx, onChange](float v) {
            if (*actTab != tabIdx) return;
            onChange(v >= 0.5f);
        };
        configWindow->AddControl(tc);
    };

    // label | read-only value
    auto addInfoRow = [&](const std::string& pfx, float y, const std::wstring& name,
                          const std::wstring& val, bool vis) {
        addLabel(pfx + "_lbl", CX,      y, LBL_W,                          ROW_H, name, 13.0f, vis);
        addLabel(pfx + "_val", VAL_X(), y, (WX + WW - 10.0f) - VAL_X(),    ROW_H, val,  13.0f, vis);
    };

    // Helper to update a label control in the window
    auto updLabel = [weakWin](const std::string& id, const std::wstring& txt) {
        if (auto w = weakWin.lock())
            for (auto& c : w->controls)
                if (c.id == id) { c.label = txt; break; }
    };

    // label | readout | [==O==] horizontal slider
    // fmtFn formats the raw float value for display; onChange applies it to config.
    auto addSliderRow = [&](const std::string& pfx, float y, const std::wstring& name,
                            bool vis, int tabIdx,
                            float sMin, float sMax, float sVal,
                            std::function<std::wstring(float)> fmtFn,
                            std::function<void(float)> onChange) {
        addLabel(pfx + "_lbl", CX,        y, LBL_W,     ROW_H, name,        13.0f, vis);
        addLabel(pfx + "_val", VAL_X(),   y, SLR_VAL_W, ROW_H, fmtFn(sVal), 13.0f, vis);
        GUIControl sc;
        sc.type             = GUIControlType::HSlider;
        sc.id               = pfx + "_sldr";
        sc.position         = Vector2(SLR_X, y + 2.0f);
        sc.size             = Vector2(SLR_W, ROW_H - 4.0f);
        sc.isVisible        = vis;
        sc.bgColor          = MyColor(0, 0, 0, 0);
        sc.hoverColor       = MyColor(0, 0, 0, 0);
        sc.bgTextureId      = int(BlitObj2DIndexType::NONE);
        sc.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        sc.sliderMin        = sMin;
        sc.sliderMax        = sMax;
        sc.sliderValue      = sVal;
        sc.onSliderChanged  = [actTab, tabIdx, pfx, updLabel, fmtFn, onChange](float v) {
            if (*actTab != tabIdx) return;
            updLabel(pfx + "_val", fmtFn(v));
            onChange(v);
        };
        configWindow->AddControl(sc);
    };

    // -----------------------------------------------------------------------
    // TITLEBAR
    // -----------------------------------------------------------------------
    {
        GUIControl tb;
        tb.type = GUIControlType::TitleBar;  tb.id = "titlebar";
        tb.position = Vector2(WX, WY);
        tb.size     = Vector2(WW, TITLEBAR_HEIGHT);
        tb.bgColor  = MyColor(0, 0, 0, 255);
        tb.txtColor = MyColor(255, 220, 80, 255);
        tb.bgTextureId = tb.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1);
        tb.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1HL);
        tb.label = L"System Configuration";  tb.lblFontSize = 16.0f;
        tb.lblCenterH = false;
        tb.isVisible = true;
        tb.onMouseBtnDown = [weakWin]() { if (auto w = weakWin.lock()) w->isDragging = true; };
        tb.onMouseBtnUp   = [weakWin]() { if (auto w = weakWin.lock()) w->isDragging = false; };
        configWindow->AddControl(tb);
    }

    // -----------------------------------------------------------------------
    // TAB NAV BUTTONS
    // -----------------------------------------------------------------------
    const float TAB_Y = WY + TITLEBAR_HEIGHT;
    const float TAB_H = 26.0f;
    const float TAB_W = WW / 5.0f;

    struct TabDef { const char* id; const wchar_t* lbl; int idx; };
    const TabDef TABS[5] = {
        { "tbn0", L"Game Play",   0 },
        { "tbn1", L"Audio",       1 },
        { "tbn2", L"Video",       2 },
        { "tbn3", L"Controls",    3 },
        { "tbn4", L"Key Mapping", 4 },
    };
    for (const auto& t : TABS) {
        GUIControl btn;
        btn.type = GUIControlType::Button;  btn.id = t.id;
        btn.position = Vector2(WX + t.idx * TAB_W, TAB_Y);
        btn.size     = Vector2(TAB_W, TAB_H);
        btn.bgColor  = MyColor(30, 30, 50, 255);
        btn.hoverColor = MyColor(60, 60, 90, 255);
        btn.bgTextureId      = int(BlitObj2DIndexType::IMG_TAB_GUNMETALGRAY);
        btn.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TAB_RED);
        btn.txtColor  = (t.idx == 0) ? MyColor(255, 220, 0, 255) : MyColor(150, 150, 150, 255);
        btn.label = t.lbl;  btn.lblFontSize = 13.0f;  btn.isVisible = true;
        int idx = t.idx;
        btn.onMouseBtnDown = [doSwitchTab, idx]() {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            doSwitchTab(idx);
        };
        configWindow->AddControl(btn);
    }

    // -----------------------------------------------------------------------
    // CONTENT BEVEL
    // -----------------------------------------------------------------------
    const float CONT_H = 370.0f;
    {
        GUIControl bevel;
        bevel.type = GUIControlType::TextArea;  bevel.id = "container";
        bevel.position = Vector2(WX, CONT_Y);
        bevel.size     = Vector2(WW, CONT_H);
        bevel.bgColor  = MyColor(15, 15, 25, 220);   // raised from 128 to match window background
        bevel.bgTextureId = bevel.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BEVEL1);
        bevel.label = L"";  bevel.isVisible = true;
        configWindow->AddControl(bevel);
    }

    // ===================================================================
    // TAB 0: GAME PLAY
    // Changes go directly into config.myConfig; camera reads fov/pitch live.
    // ===================================================================
    {
        float y = CY;

        addSliderRow("t0_zoom", y, L"Zoom Sensitivity:", true, 0,
            0.001f, 0.050f, (float)config.myConfig.zoomSensitivity,
            [](float v) { return CfgFmtFloat((long double)v, 4); },
            [](float v) { config.myConfig.zoomSensitivity = (long double)v; });
        y += ROW;

        addSliderRow("t0_move", y, L"Move Sensitivity:", true, 0,
            0.0001f, 0.0050f, (float)config.myConfig.moveSensitivity,
            [](float v) { return CfgFmtFloat((long double)v, 5); },
            [](float v) { config.myConfig.moveSensitivity = (long double)v; });
        y += ROW;

        addSliderRow("t0_maxp", y, L"Max Pitch (deg):", true, 0,
            1.0f, 89.0f, (float)config.myConfig.maxPitch,
            [](float v) { return CfgFmtFloat((long double)std::round(v), 1); },
            [](float v) {
                float s = std::round(v);
                if (s > (float)config.myConfig.minPitch + 1.0f)
                    config.myConfig.maxPitch = (long double)s;
            });
        y += ROW;

        addSliderRow("t0_minp", y, L"Min Pitch (deg):", true, 0,
            -89.0f, 88.0f, (float)config.myConfig.minPitch,
            [](float v) { return CfgFmtFloat((long double)std::round(v), 1); },
            [](float v) {
                float s = std::round(v);
                if (s < (float)config.myConfig.maxPitch - 1.0f)
                    config.myConfig.minPitch = (long double)s;
            });
        y += ROW;

        addSliderRow("t0_near", y, L"Near Plane:", true, 0,
            0.1f, 2.0f, (float)config.myConfig.nearPlane,
            [](float v) { return CfgFmtFloat((long double)v, 2); },
            [](float v) { config.myConfig.nearPlane = (long double)std::clamp(v, 0.1f, 2.0f); });
        y += ROW;

        addSliderRow("t0_far", y, L"Far Plane:", true, 0,
            500.0f, 2000.0f, (float)config.myConfig.farPlane,
            [](float v) { return CfgFmtInt((int)std::round(v)); },
            [](float v) { config.myConfig.farPlane = (long double)std::clamp(std::round(v), 500.0f, 2000.0f); });
        y += ROW;

        addTogSlider("t0_dbg", y, L"Show Debug Info:", config.myConfig.showDebugInfo, true, 0,
            [](bool on) { config.myConfig.showDebugInfo = on; });
    }

    // ===================================================================
    // TAB 1: AUDIO
    // Sliders modify config.myConfig and call config.applyLive() immediately.
    // Volumes 0-64 match the OSD / SoundManager / XMPlayer APIs.
    // ===================================================================
    {
        float y = CY;

        addSliderRow("t1_mvol", y, L"Master Volume:", false, 1,
            0.0f, 64.0f, (float)config.myConfig.masterVolume,
            [](float v) { return CfgFmtInt(std::clamp((int)std::round(v), 0, 64)); },
            [](float v) {
                config.myConfig.masterVolume = std::clamp((int)std::round(v), 0, 64);
                config.applyLive();
            });
        y += ROW;

        addSliderRow("t1_muvol", y, L"Music Volume:", false, 1,
            0.0f, 64.0f, (float)config.myConfig.musicVolume,
            [](float v) { return CfgFmtInt(std::clamp((int)std::round(v), 0, 64)); },
            [](float v) {
                config.myConfig.musicVolume = std::clamp((int)std::round(v), 0, 64);
                config.applyLive();
            });
        y += ROW;

        addSliderRow("t1_avol", y, L"Ambient Volume:", false, 1,
            0.0f, 64.0f, (float)config.myConfig.ambientVolume,
            [](float v) { return CfgFmtInt(std::clamp((int)std::round(v), 0, 64)); },
            [](float v) {
                config.myConfig.ambientVolume = std::clamp((int)std::round(v), 0, 64);
                config.applyLive();
            });
        y += ROW;

        addSliderRow("t1_dvol", y, L"Dialog Volume:", false, 1,
            0.0f, 64.0f, (float)config.myConfig.dialogVolume,
            [](float v) { return CfgFmtInt(std::clamp((int)std::round(v), 0, 64)); },
            [](float v) {
                config.myConfig.dialogVolume = std::clamp((int)std::round(v), 0, 64);
                config.applyLive();
            });
        y += ROW;

        addTogSlider("t1_pmus", y, L"Play Music:", config.myConfig.playMusic, false, 1,
            [](bool on) {
                config.myConfig.playMusic = on;
                config.applyLive();
            });
        y += ROW;

        addTogSlider("t1_tts", y, L"Text-to-Speech:", config.myConfig.UseTTS, false, 1,
            [](bool on) {
                config.myConfig.UseTTS = on;
                config.applyLive();
            });
        y += ROW;

        addSliderRow("t1_ttsvol", y, L"TTS Volume:", false, 1,
            0.0f, 5.0f, (float)config.myConfig.TTSVolume,
            [](float v) { return CfgFmtFloat((long double)v, 2); },
            [](float v) {
                config.myConfig.TTSVolume = (long double)std::clamp(v, 0.0f, 5.0f);
                config.applyLive();
            });
        y += ROW;

        addSliderRow("t1_micvol", y, L"Microphone Volume:", false, 1,
            0.0f, 20.0f, (float)config.myConfig.microphoneVolume,
            [](float v) { return CfgFmtFloat((long double)v, 2); },
            [](float v) {
                config.myConfig.microphoneVolume = (long double)std::clamp(v, 0.0f, 20.0f);
                config.applyLive();
            });
    }

    // ===================================================================
    // TAB 2: VIDEO
    // All settings flag needsVideoRestart; renderer/OS changes take effect
    // only after a game restart. The config is saved immediately on Save.
    // Row order: Display Mode, Resolution, Refresh Rate, then toggles.
    // ===================================================================
    {
        float y = CY;

        // --- Field of View (applies live; no restart needed) ---
        addSliderRow("t2_fov", y, L"Field of View:", false, 2,
            20.0f, 120.0f, (float)config.myConfig.fov,
            [](float v) { return CfgFmtFloat((long double)std::round(v), 1); },
            [](float v) {
                config.myConfig.fov = (long double)std::clamp(std::round(v), 20.0f, 120.0f);
                config.applyLive();
            });
        y += ROW;

        // --- Renderer (always 4 entries on Windows; hidden on platforms with only 1) ---
        if (RENDERER_COUNT > 1) {
            int rendSliderStart = std::clamp(config.myConfig.rendererType, 0, RENDERER_COUNT - 1);
            addSliderRow("t2_renderer", y, L"Renderer:",
                false, 2,
                0.0f, (float)(RENDERER_COUNT - 1),
                (float)rendSliderStart,
                [](float v) -> std::wstring {
                    int i = std::clamp((int)std::round(v), 0, RENDERER_COUNT - 1);
                    return AVAILABLE_RENDERERS[i].name;
                },
                [needsVideoRestart](float v) {
                    int i = std::clamp((int)std::round(v), 0, RENDERER_COUNT - 1);
                    config.myConfig.rendererType = AVAILABLE_RENDERERS[i].type;
                    *needsVideoRestart = true;
                });
            y += ROW;
        }

        // --- Display Mode (0=Windowed / 1=Borderless / 2=Full Screen) ---
        addSliderRow("t2_disp", y, L"Screen Display:",
            false, 2,
            0.0f, 2.0f, (float)std::clamp(config.myConfig.displayMode, 0, 2),
            [](float v) -> std::wstring {
                return DISP_MODE_NAMES[std::clamp((int)std::round(v), 0, 2)];
            },
            [needsVideoRestart](float v) {
                config.myConfig.displayMode = std::clamp((int)std::round(v), 0, 2);
                *needsVideoRestart = true;
            });
        y += ROW;

        // --- Resolution (slider left = lowest res, right = highest res) ---
        // uniqueRes is sorted largest-first, so invert: realIdx = (size-1) - sliderPos
        {
            int  resCount  = (int)uniqueRes->size();
            float resMax   = resCount > 0 ? (float)(resCount - 1) : 0.0f;
            float resStart = resCount > 0 ? (float)(resCount - 1 - startResIdx) : 0.0f;

            addSliderRow("t2_res", y, L"Resolution:",
                false, 2,
                0.0f, resMax, resStart,
                [uniqueRes](float v) -> std::wstring {
                    if (uniqueRes->empty()) return L"N/A";
                    int sz = (int)uniqueRes->size();
                    int i  = sz - 1 - std::clamp((int)std::round(v), 0, sz - 1);
                    auto& r = (*uniqueRes)[i];
                    return std::to_wstring(r.first) + L"x" + std::to_wstring(r.second);
                },
                [allModes, uniqueRes, resIdx, rateVec, rateIdx, needsVideoRestart,
                 weakWin, updLabel](float v) {
                    if (uniqueRes->empty()) return;
                    int sz = (int)uniqueRes->size();
                    int i  = sz - 1 - std::clamp((int)std::round(v), 0, sz - 1);
                    *resIdx = i;
                    auto& r = (*uniqueRes)[i];
                    config.myConfig.resolutionWidth  = r.first;
                    config.myConfig.resolutionHeight = r.second;
                    *rateVec = RatesFor(*allModes, r.first, r.second);
                    *rateIdx = 0;
                    if (!rateVec->empty()) config.myConfig.refreshRate = (*rateVec)[0];
                    config.myConfig.aspectRatio = LookupAspectRatio(r.first, r.second);
                    updLabel("t2_asp", CfgFmtFloat(config.myConfig.aspectRatio, 4));
                    *needsVideoRestart = true;
                    // Rebuild rate slider; default to highest rate = rightmost position
                    float newMax = rateVec->empty() ? 0.0f : (float)((int)rateVec->size() - 1);
                    if (auto w = weakWin.lock())
                        for (auto& c : w->controls)
                            if (c.id == "t2_hz_sldr") {
                                c.sliderMin = 0.0f; c.sliderMax = newMax; c.sliderValue = newMax;
                                break;
                            }
                    updLabel("t2_hz_val",
                        rateVec->empty() ? L"N/A"
                        : std::to_wstring((*rateVec)[0]) + L" Hz");
                });
            y += ROW;
        }

        // --- Refresh Rate (slider left = lowest Hz, right = highest Hz) ---
        // rateVec is sorted highest-first, so invert: realIdx = (size-1) - sliderPos
        {
            int   rateCount = (int)startRates.size();
            float rateMax   = rateCount > 0 ? (float)(rateCount - 1) : 0.0f;
            float rateStart = rateCount > 0 ? (float)(rateCount - 1 - startRateIdx) : 0.0f;

            addSliderRow("t2_hz", y, L"Refresh Rate:",
                false, 2,
                0.0f, rateMax, rateStart,
                [rateVec](float v) -> std::wstring {
                    if (rateVec->empty()) return L"N/A";
                    int sz = (int)rateVec->size();
                    int i  = sz - 1 - std::clamp((int)std::round(v), 0, sz - 1);
                    return std::to_wstring((*rateVec)[i]) + L" Hz";
                },
                [rateVec, rateIdx, needsVideoRestart](float v) {
                    if (rateVec->empty()) return;
                    int sz = (int)rateVec->size();
                    int i  = sz - 1 - std::clamp((int)std::round(v), 0, sz - 1);
                    *rateIdx = i;
                    config.myConfig.refreshRate = (*rateVec)[i];
                    *needsVideoRestart = true;
                });
            y += ROW;
        }

        // --- Toggles (all require restart) ---
        addTogSlider("t2_vsync", y, L"Vertical Sync:", config.myConfig.enableVSync, false, 2,
            [needsVideoRestart](bool on) {
                config.myConfig.enableVSync = on;
                *needsVideoRestart = true;
            });
        y += ROW;

        addTogSlider("t2_aa", y, L"Anti-Aliasing:", config.myConfig.antiAliasingEnabled, false, 2,
            [needsVideoRestart](bool on) {
                config.myConfig.antiAliasingEnabled = on;
                *needsVideoRestart = true;
            });
        y += ROW;

        addTogSlider("t2_msaa", y, L"MSAA:", config.myConfig.msaaEnabled, false, 2,
            [needsVideoRestart](bool on) {
                config.myConfig.msaaEnabled = on;
                *needsVideoRestart = true;
            });
        y += ROW;

        addTogSlider("t2_mip", y, L"Mip Mapping:", config.myConfig.MipMapping, false, 2,
            [needsVideoRestart](bool on) {
                config.myConfig.MipMapping = on;
                *needsVideoRestart = true;
            });
        y += ROW;

        addTogSlider("t2_cull", y, L"Back Face Culling:", config.myConfig.BackCulling, false, 2,
            [needsVideoRestart](bool on) {
                config.myConfig.BackCulling = on;
                *needsVideoRestart = true;
            });
        y += ROW;

        addInfoRow("t2_asp", y, L"Aspect Ratio:", CfgFmtFloat(config.myConfig.aspectRatio, 4), true);
    }

    // ===================================================================
    // TAB 3: CONTROLS  (joystick-specific; zoom/move are in Game Play)
    // ===================================================================
    {
        float y = CY;

        addSliderRow("t3_jsens", y, L"Joystick Sensitivity:", false, 3,
            0.001f, 0.100f, (float)config.myConfig.joystickSensitivity,
            [](float v) { return CfgFmtFloat((long double)v, 4); },
            [](float v) { config.myConfig.joystickSensitivity = (long double)v; });
        y += ROW;

        addSliderRow("t3_jrot", y, L"Joystick Rotation:", false, 3,
            0.0001f, 0.0100f, (float)config.myConfig.joystickRotationSensitivity,
            [](float v) { return CfgFmtFloat((long double)v, 5); },
            [](float v) { config.myConfig.joystickRotationSensitivity = (long double)v; });
    }

    // ===================================================================
    // TAB 4: KEY MAPPING (placeholder)
    // ===================================================================
    {
        float y = CY + 5.0f * ROW;
        addLabel("t4_info", CX + 50.0f, y, 560.0f, ROW_H,
            L"Key Mapping editor — coming in a future update.", 14.0f, false);
    }

    // ===================================================================
    // BOTTOM BUTTONS
    // ===================================================================
    const float BTM_Y = CONT_Y + CONT_H + 8.0f;

    // CLOSE — discard all changes, revert live audio
    addButton("btn_close", WX + 10.0f, BTM_Y, 140.0f, 34.0f, L"Close", 14.0f, true,
        [this, WIN_NAME, revertCfg]() {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            config.myConfig = *revertCfg;
            config.applyLive();
            RemoveWindow(WIN_NAME);
        }, 102);

    // SAVE — write config to disk, revert live audio to reverted state only
    // if video settings changed, show the 10-second restart notification.
    addButton("btn_save", WX + 165.0f, BTM_Y, 140.0f, 34.0f, L"Save", 14.0f, true,
        [this, WIN_NAME, needsVideoRestart]() {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            config.saveConfig();

            // Copy all captured vars to locals NOW, before RemoveWindow.
            // RemoveWindow destroys the GUIControl -> std::function -> this closure.
            // Any access to captured vars (this, WIN_NAME, needsVideoRestart) after
            // RemoveWindow is UB if the closure is the last owner.
            auto*       self       = this;
            const auto  win        = WIN_NAME;
            const bool  doRestart  = *needsVideoRestart;

            if (!doRestart) {
                RemoveWindow(win);   // last action — safe to return immediately
                return;
            }

            // Build notification window BEFORE closing config window so we never
            // access closure captures after RemoveWindow.
            const std::string NOTIFY_WIN = "restart_notify";
            const float NW = 440.0f, NH = 170.0f;
            const float NX = (static_cast<float>(self->myRenderer->iOrigWidth)  - NW) / 2.0f;
            const float NY = (static_cast<float>(self->myRenderer->iOrigHeight) - NH) / 2.0f;

            self->CreateMyWindow(NOTIFY_WIN, GUIWindowType::Dialog,
                Vector2(NX, NY), Vector2(NW, NH),
                MyColor(0, 0, 0, 220), int(BlitObj2DIndexType::NONE));

            auto nw = self->GetWindow(NOTIFY_WIN);
            if (!nw) { RemoveWindow(win); return; }
            nw->isModal = true;

            // Titlebar
            {
                GUIControl tb;
                tb.type = GUIControlType::TitleBar;  tb.id = "n_title";
                tb.position = Vector2(NX, NY);
                tb.size     = Vector2(NW - (CLOSEWINBUTTON_SIZE + 6.0f), TITLEBAR_HEIGHT);
                tb.bgColor  = MyColor(0, 0, 0, 255);
                tb.txtColor = MyColor(255, 80, 80, 255);
                tb.bgTextureId = tb.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1);
                tb.label = L"Video Settings — Restart Required";
                tb.lblFontSize = 14.0f;  tb.isVisible = true;
                nw->AddControl(tb);
            }
            // Message
            {
                GUIControl c;
                c.type = GUIControlType::TextArea;  c.id = "n_msg";
                c.position = Vector2(NX + 12.0f, NY + TITLEBAR_HEIGHT + 14.0f);
                c.size     = Vector2(NW - 24.0f, 26.0f);
                c.bgColor  = MyColor(0, 0, 0, 0);
                c.hoverColor = MyColor(0, 0, 0, 0);
                c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
                c.txtColor = MyColor(200, 200, 200, 255);
                c.label = L"Video settings saved. A restart is required to apply them.";
                c.lblFontSize = 13.0f;  c.isVisible = true;
                nw->AddControl(c);
            }
            // Countdown label (updated by background thread)
            {
                GUIControl c;
                c.type = GUIControlType::TextArea;  c.id = "n_countdown";
                c.position = Vector2(NX + 12.0f, NY + TITLEBAR_HEIGHT + 46.0f);
                c.size     = Vector2(NW - 24.0f, 26.0f);
                c.bgColor  = MyColor(0, 0, 0, 0);
                c.hoverColor = MyColor(0, 0, 0, 0);
                c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
                c.txtColor = MyColor(255, 220, 80, 255);
                c.label = L"Restarting automatically in 10 seconds...";
                c.lblFontSize = 13.0f;  c.isVisible = true;
                nw->AddControl(c);
            }

            auto restartDone = std::make_shared<std::atomic<bool>>(false);

            // Restart Now button — captures self (stack copy of this), not closure this
            {
                GUIControl c;
                c.type = GUIControlType::Button;  c.id = "n_btn_now";
                c.position = Vector2(NX + 12.0f, NY + TITLEBAR_HEIGHT + 86.0f);
                c.size     = Vector2(160.0f, 32.0f);
                c.bgColor  = MyColor(20, 20, 35, 102);
                c.hoverColor = MyColor(60, 60, 90, 255);
                c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
                c.txtColor = MyColor(210, 210, 210, 255);
                c.label = L"Restart Now";  c.lblFontSize = 13.0f;  c.isVisible = true;
                c.onMouseBtnDown = [self, NOTIFY_WIN, restartDone]() {
                    if (restartDone->exchange(true)) return;
                    soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                    self->RemoveWindow(NOTIFY_WIN);
                    threadManager.threadVars.bIsShuttingDown.store(true);
                    PostQuitMessage(0);
                    wchar_t ep[MAX_PATH]={}, ed[MAX_PATH]={};
                    GetModuleFileNameW(NULL, ep, MAX_PATH);
                    wcscpy_s(ed, ep);
                    if (wchar_t* s = wcsrchr(ed, L'\\')) *s = L'\0';
                    ShellExecuteW(NULL, L"open", ep, NULL, ed, SW_SHOWNORMAL);
                };
                nw->AddControl(c);
            }
            // Cancel Restart button
            {
                GUIControl c;
                c.type = GUIControlType::Button;  c.id = "n_btn_cancel";
                c.position = Vector2(NX + 188.0f, NY + TITLEBAR_HEIGHT + 86.0f);
                c.size     = Vector2(160.0f, 32.0f);
                c.bgColor  = MyColor(20, 20, 35, 102);
                c.hoverColor = MyColor(60, 60, 90, 255);
                c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
                c.txtColor = MyColor(210, 210, 210, 255);
                c.label = L"Cancel Restart";  c.lblFontSize = 13.0f;  c.isVisible = true;
                c.onMouseBtnDown = [self, NOTIFY_WIN, restartDone]() {
                    restartDone->store(true);
                    soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                    self->RemoveWindow(NOTIFY_WIN);
                };
                nw->AddControl(c);
            }

            // Close config window — after this the outer closure may be freed.
            // Everything below uses only local stack variables (self, NOTIFY_WIN, restartDone).
            RemoveWindow(win);

            // Countdown thread — captures self and NOTIFY_WIN by value from the stack
            std::thread([self, NOTIFY_WIN, restartDone]() {
                for (int s = 10; s > 0; --s) {
                    if (restartDone->load()) return;
                    if (auto w = self->GetWindow(NOTIFY_WIN)) {
                        for (auto& c : w->controls) {
                            if (c.id == "n_countdown") {
                                c.label = L"Restarting automatically in "
                                        + std::to_wstring(s) + L" seconds...";
                                break;
                            }
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                if (restartDone->exchange(true)) return;
                self->RemoveWindow(NOTIFY_WIN);
                threadManager.threadVars.bIsShuttingDown.store(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                wchar_t ep[MAX_PATH]={}, ed[MAX_PATH]={};
                GetModuleFileNameW(NULL, ep, MAX_PATH);
                wcscpy_s(ed, ep);
                if (wchar_t* s2 = wcsrchr(ed, L'\\')) *s2 = L'\0';
                ShellExecuteW(NULL, L"open", ep, NULL, ed, SW_SHOWNORMAL);
            }).detach();
        }, 102);

    // RESTART GAME — saves and immediately restarts (no notification needed)
    addButton("btn_restart", WX + 320.0f, BTM_Y, 170.0f, 34.0f, L"Restart Game", 13.0f, true,
        [this, WIN_NAME]() {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            config.saveConfig();
            RemoveWindow(WIN_NAME);
            threadManager.threadVars.bIsShuttingDown.store(true);
            PostQuitMessage(0);
            wchar_t ep[MAX_PATH]={}, ed[MAX_PATH]={};
            GetModuleFileNameW(NULL, ep, MAX_PATH);
            wcscpy_s(ed, ep);
            if (wchar_t* s = wcsrchr(ed, L'\\')) *s = L'\0';
            ShellExecuteW(NULL, L"open", ep, NULL, ed, SW_SHOWNORMAL);
        }, 102);

    // ===================================================================
    // Activate tab 0
    // ===================================================================
    doSwitchTab(0);

    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateConfigWindow - Window created (%d controls)",
        (int)configWindow->controls.size());
}
