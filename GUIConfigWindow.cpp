
#include "Includes.h"
#include <shellapi.h>
#include <thread>

#if defined(__USE_OPENGL__)
    #include "FXManager.h"
#elif defined(__USE_VULKAN__)
    #include "FXManager.h"
#else
    #include "FXManager.h"
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
    extern FXManager fxManager;
#elif defined(__USE_VULKAN__)
    extern FXManager fxManager;
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
        // Minimum supported resolution is 800x600
        if ((int)dm.dmPelsWidth < 800 || (int)dm.dmPelsHeight < 600) continue;
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

static int AspGcd(int a, int b) { return b ? AspGcd(b, a % b) : a; }
static std::wstring AspRatioName(int w, int h) {
    int g = AspGcd(w, h);
    return std::to_wstring(w / g) + L":" + std::to_wstring(h / g);
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
        #if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
            { 0, L"OpenGL" },
            { 1, L"Vulkan" },
        #elif defined(PLATFORM_APPLE) || defined(PLATFORM_IOS)
            { 0, L"OpenGL" },
        #else
            #error "AVAILABLE_RENDERERS is not defined for this platform"
        #endif
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

#if defined(__USE_DIRECTX_12__)
    const float WW = 620.0f, WH = 466.0f;   // +10 from 456 per user spec
#else
    const float WW = 620.0f, WH = 476.0f;   // −4 from original 480 per user spec
#endif
    const float WX = (static_cast<float>(myRenderer->iOrigWidth)  - WW) / 2.0f;
    const float WY = (static_cast<float>(myRenderer->iOrigHeight) - WH) / 2.0f;

    CreateMyWindow(WIN_NAME, GUIWindowType::Dialog,
        Vector2(WX, WY), Vector2(WW, WH),
        MyColor(0, 0, 0, 220),   // semi-opaque: was 128 (50% transparent), raised so 3D scene doesn't bleed through
        int(BlitObj2DIndexType::NONE));

    auto configWindow = GetWindow(WIN_NAME);
    if (!configWindow) return;

    // Hide during setup: the render thread snapshot-builds every frame and could
    // pick up this window before AddControl finishes, causing a race on the controls
    // vector (reallocation while iterating = dangling iterator → "string too long").
    configWindow->isVisible = false;
    configWindow->isModal = true;

    std::weak_ptr<GUIWindow> weakWin = configWindow;

    // -----------------------------------------------------------------------
    // Snapshot for revert-on-cancel
    // -----------------------------------------------------------------------
    auto revertCfg       = std::make_shared<MyConfig>(config.myConfig);
    auto needsVideoRestart = std::make_shared<bool>(false);
    auto actTab          = std::make_shared<int>(0);

    // -----------------------------------------------------------------------
    // Per-tab scroll infrastructure
    // ROW = 32, CONT_H = 370, top padding = 10
    // tabContentH[i] = 10 + numRows[i] * 32  (total virtual content height)
    // -----------------------------------------------------------------------
    const float CFG_SCROLL_W = 18.0f;   // scrollbar width in pixels
    const std::array<float, 5> tabContentH = {
        10.0f + 1.0f  * 32.0f,   // Tab 0: debug (1)
        10.0f + 8.0f  * 32.0f,   // Tab 1: 4 vols + music + TTS + TTSvol + mic (8)
        10.0f + 14.0f * 32.0f,   // Tab 2: fov/renderer/disp/res/hz/aspect/vsync/aa/msaa/mip/cull/tripbuf/near/far (14)
        10.0f + 6.0f  * 32.0f,   // Tab 3: zoom/move/maxP/minP/joystick/joystick-rot (6)
        10.0f + 6.0f  * 32.0f,   // Tab 4: placeholder
    };
    auto tabScrollY = std::make_shared<std::array<float, 5>>();
    tabScrollY->fill(0.0f);

    // Tracks the last time the scrollbar was clicked/dragged so the thumb flashes gold
    auto lastScrollClick = std::make_shared<std::atomic<uint64_t>>(0);

    // Scroll the active tab by `delta` pixels (positive = scroll down / content moves up).
    // After shifting, controls that have left the content area are hidden so they
    // don't bleed over the tab buttons or bottom bar.
    auto setTabScroll = [weakWin, actTab, tabScrollY, tabContentH, WY](float delta) {
        constexpr float kVisH    = 370.0f;   // CONT_H
        const float     contTop  = WY + 28.0f + 26.0f;   // WY + TITLEBAR_HEIGHT + TAB_H
        int   t    = *actTab;
        float maxS = std::max(0.0f, tabContentH[t] - kVisH);
        float nv   = std::clamp((*tabScrollY)[t] + delta, 0.0f, maxS);
        float shift = nv - (*tabScrollY)[t];
        if (std::abs(shift) < 0.5f) return;
        (*tabScrollY)[t] = nv;
        if (auto w = weakWin.lock())
            for (auto& c : w->controls)
                if (c.id.size() >= 3 && c.id[0] == 't' &&
                    (c.id[1] - '0') == t && c.id[2] == '_') {
                    c.position.y -= shift;
                    // Pre-cull only if COMPLETELY outside the content area.
                    // Partially-overlapping controls are handled by the renderer's
                    // PushClipRect (D2D / glScissor) for pixel-accurate clipping.
                    c.isVisible = !((c.position.y + c.size.y <= contTop) ||
                                    (c.position.y >= contTop + kVisH));
                }
    };

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

    // Unique aspect ratios available from the scanned display
    struct AspRatio { float value; std::wstring name; };
    auto aspRatios = std::make_shared<std::vector<AspRatio>>();
    {
        for (auto& m : *allModes) {
            float r = (float)m.w / (float)m.h;
            bool dup = false;
            for (auto& a : *aspRatios) if (std::abs(a.value - r) < 0.005f) { dup = true; break; }
            if (!dup) aspRatios->push_back({ r, AspRatioName(m.w, m.h) });
        }
        std::sort(aspRatios->begin(), aspRatios->end(),
            [](const AspRatio& a, const AspRatio& b) { return a.value < b.value; });
    }
    int startAspIdx = 0;
    {
        float curAsp = (float)config.myConfig.aspectRatio;
        for (int i = 0; i < (int)aspRatios->size(); ++i)
            if (std::abs((*aspRatios)[i].value - curAsp) < 0.005f) { startAspIdx = i; break; }
    }
    auto aspIdx = std::make_shared<int>(startAspIdx);

    // -----------------------------------------------------------------------
    // Tab switching — show/hide t{n}_* controls, re-colour tab buttons
    // -----------------------------------------------------------------------
    auto doSwitchTab = [weakWin, actTab, tabScrollY](int t) {
        // Restore outgoing tab's Y positions and visibility before the main
        // show/hide loop runs; clipped controls must be un-hidden first so
        // the main loop can set their final isVisible state correctly.
        int oldTab   = *actTab;
        float oldScr = (*tabScrollY)[oldTab];
        if (std::abs(oldScr) > 0.5f) {
            (*tabScrollY)[oldTab] = 0.0f;
            if (auto w = weakWin.lock())
                for (auto& c : w->controls)
                    if (c.id.size() >= 3 && c.id[0] == 't' &&
                        (c.id[1] - '0') == oldTab && c.id[2] == '_') {
                        c.position.y += oldScr;   // un-scroll
                        c.isVisible   = true;      // un-clip; main loop sets final state
                    }
        }
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
    // OpenGL GDI text is slightly wider than DWrite; reduce label/value font size so
    // value text (e.g. "Full Screen", "DirectX 11") is not cut off in its column.
#if defined(__USE_OPENGL__)
    constexpr float LBL_FS = 11.0f;
#else
    constexpr float LBL_FS = 13.0f;
#endif
    const float CX        = WX + 10.0f;
    const float CONT_Y    = WY + TITLEBAR_HEIGHT + 26.0f;
    const float CY        = CONT_Y + 10.0f;
    const float ROW       = 32.0f;

    const float SLR_X = CX + LBL_W + 5.0f + SLR_VAL_W + 5.0f;
    // Reserve CFG_SCROLL_W + 4 px (2 px gap each side) for the tab scrollbar
    const float SLR_W = (WX + WW - 10.0f - CFG_SCROLL_W - 4.0f) - SLR_X;

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
        addLabel(pfx + "_lbl", CX, y, LBL_W, ROW_H, name, LBL_FS, vis);
        GUIControl tc;
        tc.type             = GUIControlType::ToggleSlider;
        tc.id               = pfx + "_tog";
        tc.position         = Vector2(VAL_X(), y);
        tc.size             = Vector2(52.0f, ROW_H);
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
        addLabel(pfx + "_lbl", CX,      y, LBL_W,                                          ROW_H, name, LBL_FS, vis);
        addLabel(pfx + "_val", VAL_X(), y, (WX + WW - 10.0f - CFG_SCROLL_W - 4.0f) - VAL_X(), ROW_H, val,  LBL_FS, vis);
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
        addLabel(pfx + "_lbl", CX,        y, LBL_W,     ROW_H, name,        LBL_FS, vis);
        addLabel(pfx + "_val", VAL_X(),   y, SLR_VAL_W, ROW_H, fmtFn(sVal), LBL_FS, vis);
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
    // TITLEBAR — drawn entirely by onCustomRender (3-D look + circular close button)
    // The GUIControl is kept for drag-zone registration only; its own rendering is suppressed
    // by setting bgTextureId = NONE and bgColor fully transparent.
    // -----------------------------------------------------------------------
    {
        GUIControl tb;
        tb.type = GUIControlType::TitleBar;  tb.id = "titlebar";
        tb.position = Vector2(WX, WY);
        tb.size     = Vector2(WW, TITLEBAR_HEIGHT);
        tb.bgColor  = MyColor(0, 0, 0, 0);   // transparent — drawn by onCustomRender
        tb.bgTextureId = tb.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        tb.label = L"";  tb.isVisible = true;
        tb.onMouseBtnDown = [weakWin]() { if (auto w = weakWin.lock()) w->isDragging = true; };
        tb.onMouseBtnUp   = [weakWin]() { if (auto w = weakWin.lock()) w->isDragging = false; };
        configWindow->AddControl(tb);
    }

    // -----------------------------------------------------------------------
    // CIRCULAR CLOSE BUTTON — transparent Button positioned over the drawn circle.
    // Saves config and closes (mirrors Save but without restart prompt).
    // A shared atomic flag guards against double-fire.
    // -----------------------------------------------------------------------
    const float CLOSE_BTN_W = 22.0f;             // close button width
    const float CLOSE_BTN_H = TITLEBAR_HEIGHT - 4.0f;   // close button height
    {
        auto closeGuard = std::make_shared<std::atomic<bool>>(false);

        GUIControl cbBtn;
        cbBtn.type = GUIControlType::Button;  cbBtn.id = "btn_circleclose";
        cbBtn.position = Vector2(WX + WW - CLOSE_BTN_W - 3.0f, WY + 2.0f);
        cbBtn.size     = Vector2(CLOSE_BTN_W, CLOSE_BTN_H);
        cbBtn.bgColor  = MyColor(0, 0, 0, 0);       // fully transparent — circle is drawn by onCustomRender
        cbBtn.bgTextureId = cbBtn.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
        cbBtn.label = L"";  cbBtn.lblFontSize = 1.0f;  cbBtn.isVisible = true;

        cbBtn.onMouseBtnDown = [this, WIN_NAME, closeGuard]() {
            if (closeGuard->exchange(true)) return;   // prevent double-fire
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            config.saveConfig();
            RemoveWindow(WIN_NAME);
        };
        configWindow->AddControl(cbBtn);
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
#if defined(__USE_OPENGL__)
        btn.bold = true;
#endif
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
    // Row order: FOV, Renderer, Display Mode, Resolution, Refresh Rate,
    //            Aspect Ratio, toggles, Near Plane, Far Plane.
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
                 weakWin, updLabel, aspRatios, aspIdx](float v) {
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
                    {
                        float newAsp = (float)config.myConfig.aspectRatio;
                        int newAspIdx = 0;
                        for (int j = 0; j < (int)aspRatios->size(); ++j)
                            if (std::abs((*aspRatios)[j].value - newAsp) < 0.005f) { newAspIdx = j; break; }
                        *aspIdx = newAspIdx;
                        updLabel("t2_asp_val", aspRatios->empty() ? L"N/A" : (*aspRatios)[newAspIdx].name);
                        if (auto w2 = weakWin.lock())
                            for (auto& c2 : w2->controls)
                                if (c2.id == "t2_asp_sldr") { c2.sliderValue = (float)newAspIdx; break; }
                    }
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

        // --- Aspect Ratio (auto-updated when Resolution changes) ---
        {
            float aspMax = aspRatios->empty() ? 0.0f : (float)((int)aspRatios->size() - 1);
            addSliderRow("t2_asp", y, L"Aspect Ratio:",
                false, 2,
                0.0f, aspMax, (float)startAspIdx,
                [aspRatios](float v) -> std::wstring {
                    if (aspRatios->empty()) return L"N/A";
                    int i = std::clamp((int)std::round(v), 0, (int)aspRatios->size() - 1);
                    return (*aspRatios)[i].name;
                },
                [aspRatios, aspIdx, needsVideoRestart](float v) {
                    if (aspRatios->empty()) return;
                    int i = std::clamp((int)std::round(v), 0, (int)aspRatios->size() - 1);
                    *aspIdx = i;
                    config.myConfig.aspectRatio = (long double)(*aspRatios)[i].value;
                    *needsVideoRestart = true;
                });
        }
        y += ROW;

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

        addTogSlider("t2_tripbuf", y, L"Triple Buffering:", config.myConfig.buffering != 0, false, 2,
            [needsVideoRestart](bool on) {
                config.myConfig.buffering = on ? 1 : 0;
                *needsVideoRestart = true;
            });
        y += ROW;

        // --- Near / Far Plane (apply live; moved from Game Play tab) ---
        addSliderRow("t2_near", y, L"Near Plane:", false, 2,
            0.1f, 2.0f, (float)config.myConfig.nearPlane,
            [](float v) { return CfgFmtFloat((long double)v, 2); },
            [](float v) { config.myConfig.nearPlane = (long double)std::clamp(v, 0.1f, 2.0f); });
        y += ROW;

        addSliderRow("t2_far", y, L"Far Plane:", false, 2,
            500.0f, 2000.0f, (float)config.myConfig.farPlane,
            [](float v) { return CfgFmtInt((int)std::round(v)); },
            [](float v) { config.myConfig.farPlane = (long double)std::clamp(std::round(v), 500.0f, 2000.0f); });
    }

    // ===================================================================
    // TAB 3: CONTROLS
    // ===================================================================
    {
        float y = CY;

        addSliderRow("t3_zoom", y, L"Zoom Sensitivity:", false, 3,
            0.001f, 0.050f, (float)config.myConfig.zoomSensitivity,
            [](float v) { return CfgFmtFloat((long double)v, 4); },
            [](float v) { config.myConfig.zoomSensitivity = (long double)v; });
        y += ROW;

        addSliderRow("t3_move", y, L"Move Sensitivity:", false, 3,
            0.0001f, 0.0050f, (float)config.myConfig.moveSensitivity,
            [](float v) { return CfgFmtFloat((long double)v, 5); },
            [](float v) { config.myConfig.moveSensitivity = (long double)v; });
        y += ROW;

        addSliderRow("t3_maxp", y, L"Max Pitch (deg):", false, 3,
            1.0f, 89.0f, (float)config.myConfig.maxPitch,
            [](float v) { return CfgFmtFloat((long double)std::round(v), 1); },
            [](float v) {
                float s = std::round(v);
                if (s > (float)config.myConfig.minPitch + 1.0f)
                    config.myConfig.maxPitch = (long double)s;
            });
        y += ROW;

        addSliderRow("t3_minp", y, L"Min Pitch (deg):", false, 3,
            -89.0f, 88.0f, (float)config.myConfig.minPitch,
            [](float v) { return CfgFmtFloat((long double)std::round(v), 1); },
            [](float v) {
                float s = std::round(v);
                if (s < (float)config.myConfig.maxPitch - 1.0f)
                    config.myConfig.minPitch = (long double)s;
            });
        y += ROW;

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

            // Build 3D notification window BEFORE closing config window so we never
            // access closure captures after RemoveWindow.
            const std::string NOTIFY_WIN = "restart_notify";
#if defined(__USE_OPENGL__)
            const float NW = 420.0f, NH = 188.0f;
#else
            const float NW = 440.0f, NH = 188.0f;
#endif
            const float NX = (static_cast<float>(self->myRenderer->iOrigWidth)  - NW) / 2.0f;
            const float NY = (static_cast<float>(self->myRenderer->iOrigHeight) - NH) / 2.0f;

            // Solid dark background so controls render correctly.
            // onCustomRender only draws the bevel border + titlebar area on top.
            self->CreateMyWindow(NOTIFY_WIN, GUIWindowType::Dialog,
                Vector2(NX, NY), Vector2(NW, NH),
                MyColor(18, 20, 32, 252), int(BlitObj2DIndexType::NONE));

            auto nw = self->GetWindow(NOTIFY_WIN);
            if (!nw) { RemoveWindow(win); return; }
            nw->isModal = true;

            // Message
            {
                GUIControl c;
                c.type = GUIControlType::TextArea;  c.id = "n_msg";
                c.position = Vector2(NX + 14.0f, NY + TITLEBAR_HEIGHT + 16.0f);
                c.size     = Vector2(NW - 28.0f, 26.0f);
                c.bgColor  = MyColor(0, 0, 0, 0);
                c.hoverColor = MyColor(0, 0, 0, 0);
                c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
                c.txtColor = MyColor(205, 205, 210, 255);
                c.label = L"Video settings saved. A restart is required to apply them.";
                c.lblFontSize = 13.0f;  c.isVisible = true;
                nw->AddControl(c);
            }
            // Countdown label (updated by background thread)
            {
                GUIControl c;
                c.type = GUIControlType::TextArea;  c.id = "n_countdown";
                c.position = Vector2(NX + 14.0f, NY + TITLEBAR_HEIGHT + 50.0f);
                c.size     = Vector2(NW - 28.0f, 26.0f);
                c.bgColor  = MyColor(0, 0, 0, 0);
                c.hoverColor = MyColor(0, 0, 0, 0);
                c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
                c.txtColor = MyColor(255, 218, 75, 255);
                c.label = L"Restarting automatically in 30 seconds...";
                c.lblFontSize = 13.0f;  c.isVisible = true;
                nw->AddControl(c);
            }

            auto restartDone = std::make_shared<std::atomic<bool>>(false);

            const float N_BTN_Y = NY + TITLEBAR_HEIGHT + 100.0f;

            // Restart Now button
            {
                GUIControl c;
                c.type = GUIControlType::Button;  c.id = "n_btn_now";
                c.position = Vector2(NX + 14.0f, N_BTN_Y);
                c.size     = Vector2(168.0f, 34.0f);
                c.bgColor  = MyColor(20, 20, 35, 102);
                c.hoverColor = MyColor(60, 60, 90, 255);
                c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
                c.txtColor = MyColor(215, 215, 215, 255);
                c.bold = true;
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
                c.position = Vector2(NX + 196.0f, N_BTN_Y);
                c.size     = Vector2(168.0f, 34.0f);
                c.bgColor  = MyColor(20, 20, 35, 102);
                c.hoverColor = MyColor(60, 60, 90, 255);
                c.bgTextureId = c.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
                c.txtColor = MyColor(215, 215, 215, 255);
                c.bold = true;
                c.label = L"Cancel Restart";  c.lblFontSize = 13.0f;  c.isVisible = true;
                c.onMouseBtnDown = [self, NOTIFY_WIN, restartDone]() {
                    restartDone->store(true);
                    soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                    self->RemoveWindow(NOTIFY_WIN);
                };
                nw->AddControl(c);
            }

            // 3D window frame + solid gradient titlebar drawn by onCustomRender
            // All positions are captured by value (NX/NY are fixed — Dialog windows don't drag)
            const float kNTH = TITLEBAR_HEIGHT;   // local copy so lambda can capture it
            nw->onCustomRender = [NX, NY, NW, NH, kNTH](Renderer* r2) {
                // ── OUTER RAISED BEVEL BORDER (edges only — body already filled by window bg)
                // Highlight edges (top + left = light)
                r2->DrawRectangle(Vector2(NX, NY), Vector2(NW, 1.0f),
                    MyColor(95, 105, 145, 240), true);
                r2->DrawRectangle(Vector2(NX, NY), Vector2(1.0f, NH),
                    MyColor(88, 98, 138, 228), true);
                // Shadow edges (bottom + right = dark)
                r2->DrawRectangle(Vector2(NX, NY + NH - 1.0f), Vector2(NW, 1.0f),
                    MyColor(4, 5, 10, 240), true);
                r2->DrawRectangle(Vector2(NX + NW - 1.0f, NY), Vector2(1.0f, NH),
                    MyColor(4, 5, 10, 240), true);
                // Inner bevel (second depth level)
                r2->DrawRectangle(Vector2(NX + 1.0f, NY + 1.0f), Vector2(NW - 2.0f, 1.0f),
                    MyColor(68, 78, 112, 200), true);
                r2->DrawRectangle(Vector2(NX + 1.0f, NY + 1.0f), Vector2(1.0f, NH - 2.0f),
                    MyColor(62, 72, 105, 185), true);
                r2->DrawRectangle(Vector2(NX + 1.0f, NY + NH - 2.0f), Vector2(NW - 2.0f, 1.0f),
                    MyColor(8, 9, 16, 220), true);
                r2->DrawRectangle(Vector2(NX + NW - 2.0f, NY + 1.0f), Vector2(1.0f, NH - 2.0f),
                    MyColor(8, 9, 16, 220), true);

                // ── SMOOTH GRADIENT TITLEBAR (per-pixel linear interpolation, 1px bands) ──
                {
                    struct GStop2 { float t, r, g, b, a; };
                    static const GStop2 kStops2[] = {
                        {0.00f, 162.f, 205.f, 255.f, 210.f},   // top: sky sheen
                        {0.15f, 110.f, 165.f, 248.f, 220.f},   // bright blue
                        {0.35f,  62.f, 110.f, 218.f, 230.f},   // royal blue
                        {0.55f,  28.f,  58.f, 148.f, 240.f},   // cobalt
                        {0.75f,  12.f,  24.f,  72.f, 248.f},   // dark navy
                        {1.00f,   4.f,   6.f,  22.f, 255.f},   // near-black
                    };
                    const int nS2   = (int)(sizeof(kStops2) / sizeof(kStops2[0]));
                    const float tbH = kNTH - 2.0f;
                    const int kRows = std::max(1, (int)tbH);
                    for (int row = 0; row < kRows; ++row) {
                        float t = (kRows > 1) ? (float)row / (float)(kRows - 1) : 0.f;
                        float cr = 4.f, cg = 6.f, cb = 22.f, ca = 255.f;
                        for (int si = 0; si < nS2 - 1; ++si) {
                            if (t >= kStops2[si].t && t <= kStops2[si + 1].t) {
                                float denom = kStops2[si + 1].t - kStops2[si].t;
                                float u = (denom > 0.f) ? (t - kStops2[si].t) / denom : 0.f;
                                cr = kStops2[si].r + u * (kStops2[si + 1].r - kStops2[si].r);
                                cg = kStops2[si].g + u * (kStops2[si + 1].g - kStops2[si].g);
                                cb = kStops2[si].b + u * (kStops2[si + 1].b - kStops2[si].b);
                                ca = kStops2[si].a + u * (kStops2[si + 1].a - kStops2[si].a);
                                break;
                            }
                        }
                        r2->DrawRectangle(Vector2(NX + 2.0f, NY + 2.0f + (float)row),
                            Vector2(NW - 4.0f, 1.0f),
                            MyColor((uint8_t)cr, (uint8_t)cg, (uint8_t)cb, (uint8_t)ca), true);
                    }
                }
                // Divider between titlebar and content
                r2->DrawRectangle(Vector2(NX + 2.0f, NY + kNTH - 1.0f),
                    Vector2(NW - 4.0f, 1.0f), MyColor(2, 3, 8, 255), true);
                r2->DrawRectangle(Vector2(NX + 2.0f, NY + kNTH),
                    Vector2(NW - 4.0f, 1.0f), MyColor(48, 55, 85, 180), true);

                // Title text — bold, centred in titlebar
                r2->DrawMyTextCentered(L"Video Settings - Restart Required",
                    Vector2(NX + 2.0f, NY + 2.0f),
                    MyColor(255, 82, 82, 255), 13.0f,
                    NW - 4.0f, kNTH - 2.0f, true);
            };

            // Close config window — after this the outer closure may be freed.
            // Everything below uses only local stack variables (self, NOTIFY_WIN, restartDone).
            RemoveWindow(win);

            // Countdown thread — captures self and NOTIFY_WIN by value from the stack
            std::thread([self, NOTIFY_WIN, restartDone]() {
                for (int s = 30; s > 0; --s) {
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

#if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
    for (auto& c : configWindow->controls) {
        if (c.type == GUIControlType::Button &&
            (c.id == "btn_close" || c.id == "btn_save" || c.id == "btn_restart"))
            c.bold = true;
    }
#endif

    // ===================================================================
    // Activate tab 0
    // ===================================================================
    doSwitchTab(0);

    // ===================================================================
    // Clip rect + clipContent — marks all tab content controls so
    // GUIWindow::Render() scissors them inside the bevel box via
    // renderer PushClipRect (D2D / glScissor).  This must run AFTER
    // all controls are added so the container/titlebar/tab-buttons
    // are NOT flagged (they render outside the clip region).
    // ===================================================================
    for (auto& c : configWindow->controls) {
        if (c.id.size() >= 3 && c.id[0] == 't' &&
            std::isdigit(static_cast<unsigned char>(c.id[1])) && c.id[2] == '_')
            c.clipContent = true;
    }
    configWindow->m_hasClip  = true;
    configWindow->m_clipPos  = Vector2(WX, CONT_Y + 4.0f);
    configWindow->m_clipSize = Vector2(WW, CONT_H - 9.0f);

    // ===================================================================
    // SCROLLBAR — drawn on top of all controls via onCustomRender.
    // Track position per spec: y+3, x−3, height−4 relative to content area.
    // Thumb height is proportional to visible / total content height.
    // When the active tab's content fits the visible area the thumb fills
    // the full track (inactive appearance).
    // ===================================================================
    {
        // SCROLL_H is the only scrollbar dimension not derivable from the window position.
        // All X/Y positions are computed live inside the lambdas from weakWin->position.
        const float SCROLL_H = CONT_H - 11.0f;   // −8 baseline then −3 per spec

        // onCustomRender: all positions derived from weakWin at render time so that
        // window dragging keeps titlebar, circle button, and scrollbar in sync.
        configWindow->onCustomRender = [weakWin, actTab, tabScrollY, tabContentH,
                                         CFG_SCROLL_W, SCROLL_H, lastScrollClick](Renderer* r) {
            auto w = weakWin.lock();
            if (!w) return;

            constexpr float kTH   = TITLEBAR_HEIGHT;
            constexpr float kVisH = 370.0f;
            const float wx  = w->position.x;
            const float wy  = w->position.y;
            const float ww  = w->size.x;
            const float wh  = w->size.y;

            // ── WINDOW DROP SHADOW ────────────────────────────────────────
            r->DrawRectangle(Vector2(wx + 6.0f, wy + 8.0f), Vector2(ww, wh),
                MyColor(0, 0, 0, 95), true);

            // ── SMOOTH GRADIENT TITLEBAR (per-pixel linear interpolation, 1px bands) ──
            {
                struct GStop { float t, r, g, b, a; };
                static const GStop kStops[] = {
                    {0.00f, 195.f, 225.f, 255.f, 200.f},   // top: bright glass cap
                    {0.06f, 162.f, 205.f, 255.f, 210.f},   // near-white sheen
                    {0.12f, 125.f, 178.f, 255.f, 218.f},   // sky-blue highlight
                    {0.25f,  95.f, 148.f, 245.f, 225.f},   // royal blue
                    {0.38f,  68.f, 108.f, 210.f, 232.f},   // mid-blue
                    {0.50f,  42.f,  68.f, 158.f, 238.f},   // cobalt
                    {0.62f,  22.f,  35.f,  95.f, 245.f},   // dark navy
                    {0.74f,  10.f,  16.f,  50.f, 248.f},   // deep navy
                    {0.85f,   6.f,   8.f,  28.f, 252.f},   // near-black navy
                    {1.00f,   2.f,   3.f,  12.f, 255.f},   // bottom shadow
                };
                const int nS    = (int)(sizeof(kStops) / sizeof(kStops[0]));
                const int kRows = std::max(1, (int)kTH);
                for (int row = 0; row < kRows; ++row) {
                    float t = (kRows > 1) ? (float)row / (float)(kRows - 1) : 0.f;
                    float cr = 2.f, cg = 3.f, cb = 12.f, ca = 255.f;
                    for (int si = 0; si < nS - 1; ++si) {
                        if (t >= kStops[si].t && t <= kStops[si + 1].t) {
                            float denom = kStops[si + 1].t - kStops[si].t;
                            float u = (denom > 0.f) ? (t - kStops[si].t) / denom : 0.f;
                            cr = kStops[si].r + u * (kStops[si + 1].r - kStops[si].r);
                            cg = kStops[si].g + u * (kStops[si + 1].g - kStops[si].g);
                            cb = kStops[si].b + u * (kStops[si + 1].b - kStops[si].b);
                            ca = kStops[si].a + u * (kStops[si + 1].a - kStops[si].a);
                            break;
                        }
                    }
                    r->DrawRectangle(Vector2(wx, wy + (float)row), Vector2(ww, 1.0f),
                        MyColor((uint8_t)cr, (uint8_t)cg, (uint8_t)cb, (uint8_t)ca), true);
                }
            }
            // Top edge glass line
            r->DrawRectangle(Vector2(wx + 1.0f, wy + 1.0f), Vector2(ww - 2.0f, 1.0f),
                MyColor(220, 240, 255, 120), true);
            // Bottom edge hard divider
            r->DrawRectangle(Vector2(wx, wy + kTH - 1.0f), Vector2(ww, 1.0f),
                MyColor(2, 3, 10, 255), true);
            // Outer raised bevel on titlebar edges
            r->DrawRectangle(Vector2(wx, wy), Vector2(ww, 1.0f),
                MyColor(110, 125, 165, 230), true);
            r->DrawRectangle(Vector2(wx, wy), Vector2(1.0f, kTH),
                MyColor(100, 118, 158, 220), true);
            r->DrawRectangle(Vector2(wx + ww - 1.0f, wy), Vector2(1.0f, kTH),
                MyColor(4, 5, 14, 220), true);

            // Title text — left-aligned, vertically centred, 16pt pseudo-bold
            // DrawMyText has no bold param; drawing twice at +1px x gives the bold weight.
            {
                constexpr float kTitleFS = 16.0f;
                const float tbTextY = wy + (kTH - kTitleFS * 1.1f) * 0.5f;
                // Back pass (1px right) at reduced alpha — widens strokes
                r->DrawMyText(L"System Configuration",
                    Vector2(wx + 9.0f, tbTextY), MyColor(255, 228, 90, 160), kTitleFS);
                // Front pass — full alpha, correct position
                r->DrawMyText(L"System Configuration",
                    Vector2(wx + 8.0f, tbTextY), MyColor(255, 228, 90, 255), kTitleFS);
            }

            // ── RECTANGULAR CLOSE BUTTON ──────────────────────────────────
            const float clBtnW = 22.0f;
            const float clBtnH = kTH - 4.0f;
            const float clBtnX = wx + ww - clBtnW - 3.0f;
            const float clBtnY = wy + 2.0f;

            // Button drop shadow
            r->DrawRectangle(Vector2(clBtnX + 1.5f, clBtnY + 1.5f), Vector2(clBtnW, clBtnH),
                MyColor(0, 0, 0, 105), true);
            // Button body base (dark red)
            r->DrawRectangle(Vector2(clBtnX, clBtnY), Vector2(clBtnW, clBtnH),
                MyColor(165, 30, 26, 255), true);
            // Upper half brighter
            r->DrawRectangle(Vector2(clBtnX, clBtnY), Vector2(clBtnW, std::floor(clBtnH * 0.50f)),
                MyColor(210, 52, 46, 255), true);
            // Top glass line
            r->DrawRectangle(Vector2(clBtnX + 1.0f, clBtnY + 1.0f), Vector2(clBtnW - 2.0f, 1.0f),
                MyColor(255, 165, 158, 88), true);
            // Raised outer bevel
            r->DrawRectangle(Vector2(clBtnX, clBtnY), Vector2(clBtnW, 1.0f),
                MyColor(228, 88, 82, 220), true);
            r->DrawRectangle(Vector2(clBtnX, clBtnY), Vector2(1.0f, clBtnH),
                MyColor(218, 78, 72, 200), true);
            r->DrawRectangle(Vector2(clBtnX, clBtnY + clBtnH - 1.0f), Vector2(clBtnW, 1.0f),
                MyColor(78, 10, 8, 220), true);
            r->DrawRectangle(Vector2(clBtnX + clBtnW - 1.0f, clBtnY), Vector2(1.0f, clBtnH),
                MyColor(78, 10, 8, 220), true);
            // Bold "X" symbol centred
            r->DrawMyTextCentered(L"X",
                Vector2(clBtnX, clBtnY), MyColor(255, 228, 220, 255), 13.0f,
                clBtnW, clBtnH, true);

            // ── SCROLLBAR ─────────────────────────────────────────────────
            const float contY   = wy + kTH + 26.0f;
            const float scrollX = wx + ww - CFG_SCROLL_W - 7.0f;
            const float scrollY = contY + 5.0f;

            int   t         = *actTab;
            float contentH  = tabContentH[t];
            float scrollOff = (*tabScrollY)[t];
            float maxS      = std::max(0.0f, contentH - kVisH);
            const bool hasScroll = (maxS > 0.0f);

            // Track outer background
            r->DrawRectangle(Vector2(scrollX, scrollY),
                Vector2(CFG_SCROLL_W, SCROLL_H), MyColor(12, 14, 22, 245), true);
            // Track sunken bevel (top-left dark, bottom-right light)
            r->DrawRectangle(Vector2(scrollX, scrollY),
                Vector2(CFG_SCROLL_W, 1.0f), MyColor(4, 5, 12, 230), true);
            r->DrawRectangle(Vector2(scrollX, scrollY),
                Vector2(1.0f, SCROLL_H), MyColor(4, 5, 12, 230), true);
            r->DrawRectangle(Vector2(scrollX, scrollY + SCROLL_H - 1.0f),
                Vector2(CFG_SCROLL_W, 1.0f), MyColor(62, 68, 92, 200), true);
            r->DrawRectangle(Vector2(scrollX + CFG_SCROLL_W - 1.0f, scrollY),
                Vector2(1.0f, SCROLL_H), MyColor(62, 68, 92, 200), true);
            // Inner trough
            r->DrawRectangle(Vector2(scrollX + 1.0f, scrollY + 1.0f),
                Vector2(CFG_SCROLL_W - 2.0f, SCROLL_H - 2.0f),
                MyColor(8, 10, 18, 255), true);

            // Scrollbar thumb
            float thumbH = hasScroll
                ? std::max(24.0f, SCROLL_H * (kVisH / contentH))
                : SCROLL_H - 4.0f;
            float thumbY = hasScroll
                ? scrollY + 1.0f + (SCROLL_H - 2.0f - thumbH) * (scrollOff / maxS)
                : scrollY + 2.0f;

            const float tx = scrollX + 2.0f;
            const float tw = CFG_SCROLL_W - 4.0f;

            // Active = scrollbar was clicked/dragged within the last 600 ms
            const bool  scrollActive = (GetTickCount64() - lastScrollClick->load()) < 600;
            const bool  scrollFlash  = scrollActive && ((GetTickCount64() / 220) % 2 == 0);

            // Thumb drop shadow
            r->DrawRectangle(Vector2(tx + 1.0f, thumbY + 1.0f), Vector2(tw, thumbH),
                MyColor(0, 0, 0, 88), true);

            // Thumb body — gold when scrollable, dark when at full size (no scroll needed)
            // Flash between bright and dimmer gold when actively dragging
            float thumbHalf = std::floor(thumbH * 0.5f);
            MyColor goldTop, goldBot;
            if (hasScroll) {
                if (scrollFlash) {
                    goldTop = MyColor(255, 235, 80,  255);   // bright flash top
                    goldBot = MyColor(215, 175, 28,  255);   // bright flash bottom
                } else if (scrollActive) {
                    goldTop = MyColor(235, 205, 50,  250);   // active top
                    goldBot = MyColor(190, 148, 18,  250);   // active bottom
                } else {
                    goldTop = MyColor(210, 178, 40,  238);   // idle gold top
                    goldBot = MyColor(165, 128, 12,  238);   // idle gold bottom
                }
            } else {
                goldTop = MyColor(42, 44, 60, 180);
                goldBot = MyColor(30, 32, 44, 180);
            }
            r->DrawRectangle(Vector2(tx, thumbY), Vector2(tw, thumbHalf), goldTop, true);
            r->DrawRectangle(Vector2(tx, thumbY + thumbHalf),
                Vector2(tw, thumbH - thumbHalf), goldBot, true);

            // Thumb raised bevel — gold highlight edges
            r->DrawRectangle(Vector2(tx, thumbY), Vector2(tw, 1.0f),
                MyColor(255, 245, 140, hasScroll ? 210 : 100), true);
            r->DrawRectangle(Vector2(tx, thumbY), Vector2(1.0f, thumbH),
                MyColor(245, 230, 110, hasScroll ? 195 : 90), true);
            r->DrawRectangle(Vector2(tx, thumbY + thumbH - 1.0f), Vector2(tw, 1.0f),
                MyColor(80, 55, 5, hasScroll ? 210 : 100), true);
            r->DrawRectangle(Vector2(tx + tw - 1.0f, thumbY), Vector2(1.0f, thumbH),
                MyColor(80, 55, 5, hasScroll ? 210 : 100), true);

            // Thumb grip lines (3 horizontal dashes centred in thumb)
            if (hasScroll && thumbH >= 18.0f) {
                float gCX = tx + tw * 0.5f;
                float gCY = thumbY + thumbH * 0.5f;
                for (int gi = -1; gi <= 1; ++gi) {
                    float gy = gCY + gi * 3.0f;
                    r->DrawRectangle(Vector2(gCX - 4.0f, gy - 1.0f),
                        Vector2(8.0f, 1.0f), MyColor(255, 245, 180, 55), true);
                    r->DrawRectangle(Vector2(gCX - 4.0f, gy),
                        Vector2(8.0f, 1.0f), MyColor(0, 0, 0, 80), true);
                }
            }
        };

        // Mouse-wheel: 18 px per notch, positive delta = scroll up
        configWindow->onMouseWheel = [setTabScroll](int delta) {
            setTabScroll(static_cast<float>(-delta) * 18.0f);
        };

        // Scrollbar track click: jump to proportional position.
        // Close-button clicks are handled by the "btn_circleclose" Button control.
        configWindow->onCustomMouseInput = [weakWin, actTab, tabScrollY, tabContentH,
                                             CFG_SCROLL_W, SCROLL_H, setTabScroll,
                                             lastScrollClick](float mx, float my) {
            auto w = weakWin.lock();
            if (!w) return;
            constexpr float kVisH = 370.0f;
            const float contY   = w->position.y + TITLEBAR_HEIGHT + 26.0f;
            const float scrollX = w->position.x + w->size.x - CFG_SCROLL_W - 7.0f;
            const float scrollY = contY + 5.0f;

            if (mx < scrollX || mx > scrollX + CFG_SCROLL_W) return;
            if (my < scrollY || my > scrollY + SCROLL_H)     return;

            // Mark scrollbar active so thumb flashes gold while dragging
            lastScrollClick->store(GetTickCount64());

            int   t    = *actTab;
            float maxS = std::max(0.0f, tabContentH[t] - kVisH);
            if (maxS <= 0.0f) return;
            float rel    = (my - scrollY) / SCROLL_H;
            float target = std::clamp(rel * maxS, 0.0f, maxS);
            float delta  = target - (*tabScrollY)[t];
            setTabScroll(delta);
        };
    }

    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateConfigWindow - Window created (%d controls)",
        (int)configWindow->controls.size());

    // All controls added — safe to make visible now
    configWindow->isVisible = true;
}
