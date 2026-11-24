// -------------------------------------------------------------------------------------------------------------
// windows-opengl-render.cpp — Windows OpenGL implementation
// -------------------------------------------------------------------------------------------------------------
// Platform-specific implementation of OpenGLRenderer, split out of OpenGLRenderer.cpp /
// OpenGLRenderFrame.cpp (platform split, 24 Nov 2026) so that working on one platform
// means working in one file. This file is the platform: it contains NO platform guards
// and is compiled only on its platform (Linux/CMakeLists.txt, CrossPlatformGameEngine.vcxproj).
// The code below is moved verbatim from the shared files; behaviour is unchanged.
// -------------------------------------------------------------------------------------------------------------
#define NOMINMAX
#include "Includes.h"
#include "windows-opengl-render.h"

#if defined(__USE_OPENGL__)

// External globals used by the moved code (mirrors OpenGLRenderFrame.cpp).
extern SceneManager scene;
extern FXManager fxManager;
extern MoviePlayer moviePlayer;
extern WindowMetrics winMetrics;

bool OpenGLRenderer::SetWindowedScreen()
{
    // Restore display settings if we entered exclusive fullscreen via ChangeDisplaySettingsEx.
    if (m_isExclusiveFullscreen) {
        const wchar_t* deviceName = m_exclusiveMonitorDevice.empty() ? nullptr
                                                                      : m_exclusiveMonitorDevice.c_str();
        ChangeDisplaySettingsExW(deviceName, nullptr, nullptr, 0, nullptr);
        m_isExclusiveFullscreen = false;
        m_exclusiveMonitorDevice.clear();
    }

    HWND hWnd = m_glContext.windowHandle;
    int rw, rh;
    if (m_savedWindowState.valid) {
        SetWindowLongW(hWnd, GWL_STYLE,   m_savedWindowState.style);
        SetWindowLongW(hWnd, GWL_EXSTYLE, m_savedWindowState.exStyle);
        RECT r = m_savedWindowState.rect;
        rw = r.right  - r.left;
        rh = r.bottom - r.top;
        SetWindowPos(hWnd, nullptr,
            r.left, r.top, rw, rh,
            SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        m_savedWindowState.valid = false;
    } else {
        // Fallback: use previous windowed dimensions.
        rw = static_cast<int>(prevWindowedWidth);
        rh = static_cast<int>(prevWindowedHeight);
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
        SetWindowLongW(hWnd, GWL_STYLE, style);
        SetWindowPos(hWnd, nullptr,
            100, 100, rw, rh,
            SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    // Use the client area dimensions, NOT the window rect (rw/rh include title bar +
    // borders). SetWindowPos is synchronous so GetClientRect returns the final size.
    {
        RECT clientRc = {};
        if (GetClientRect(hWnd, &clientRc) && clientRc.right > 0 && clientRc.bottom > 0) {
            iOrigWidth  = clientRc.right;
            iOrigHeight = clientRc.bottom;
        } else {
            iOrigWidth  = rw;
            iOrigHeight = rh;
        }
    }
    m_renderTargetWidth  = iOrigWidth;
    m_renderTargetHeight = iOrigHeight;
    m_pendingContextRebind.store(true);
    IsWindowMode.store(true);
    winMetrics.width          = iOrigWidth;
    winMetrics.height         = iOrigHeight;
    winMetrics.clientWidth    = iOrigWidth;
    winMetrics.clientHeight   = iOrigHeight;
    winMetrics.isFullScreen   = false;
    winMetrics.isBorderless   = false;
    return true;
}

bool OpenGLRenderer::SetFullExclusive(uint32_t width, uint32_t height)
{
    HWND hWnd = m_glContext.windowHandle;

    // Capture the original desktop mode and target monitor before switching.
    if (!m_isExclusiveFullscreen) {
        m_originalDesktopMode.dmSize = sizeof(DEVMODE);
        EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS,
            reinterpret_cast<DEVMODEW*>(&m_originalDesktopMode));

        // Record the monitor device name so we can restore via ChangeDisplaySettingsEx.
        HMONITOR hmon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW monInfo{};
        monInfo.cbSize = sizeof(monInfo);
        if (GetMonitorInfoW(hmon, &monInfo)) {
            m_exclusiveMonitorDevice = monInfo.szDevice;
            m_exclusiveMonitorLeft   = monInfo.rcMonitor.left;
            m_exclusiveMonitorTop    = monInfo.rcMonitor.top;
        } else {
            m_exclusiveMonitorDevice.clear();
            m_exclusiveMonitorLeft = 0;
            m_exclusiveMonitorTop  = 0;
        }
    }

    DEVMODEW dm = {};
    dm.dmSize       = sizeof(dm);
    dm.dmPelsWidth  = width;
    dm.dmPelsHeight = height;
    dm.dmBitsPerPel = 32;
    dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    if (config.myConfig.refreshRate > 0) {
        dm.dmDisplayFrequency = config.myConfig.refreshRate;
        dm.dmFields |= DM_DISPLAYFREQUENCY;
    }

    // Use ChangeDisplaySettingsEx to target the specific monitor rather than always
    // defaulting to the primary display.
    const wchar_t* deviceName = m_exclusiveMonitorDevice.empty() ? nullptr
                                                                  : m_exclusiveMonitorDevice.c_str();
    if (ChangeDisplaySettingsExW(deviceName, &dm, nullptr, CDS_FULLSCREEN, nullptr)
            != DISP_CHANGE_SUCCESSFUL) {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[OpenGLRenderer] SetFullExclusive: ChangeDisplaySettingsEx failed");
        return false;
    }
    m_isExclusiveFullscreen = true;
    IsWindowMode.store(false);

    // Gate the WM_SIZE fired by SetWindowPos so the resize handler ignores it.
    threadManager.threadVars.bSettingFullScreen.store(true);

    // Save window state for restoration (before any window changes).
    if (!m_savedWindowState.valid) {
        m_savedWindowState.style   = GetWindowLong(hWnd, GWL_STYLE);
        m_savedWindowState.exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        GetWindowRect(hWnd, &m_savedWindowState.rect);
        m_savedWindowState.valid   = true;
    }

    // Use the monitor's pre-change virtual-desktop position for window placement.
    // Dimensions come from the requested width/height (not GetMonitorInfo) because
    // ChangeDisplaySettingsEx may not have fully propagated by the time GetMonitorInfo
    // is called, which would return stale windowed dimensions.  The monitor's POSITION
    // does not change with CDS_FULLSCREEN, so the pre-change left/top is reliable.
    EnforceFullscreenWindowPlacement(
        m_exclusiveMonitorLeft,
        m_exclusiveMonitorTop,
        static_cast<int>(width),
        static_cast<int>(height),
        L"exclusive");
    bool result = true;

    iOrigWidth           = static_cast<int>(width);
    iOrigHeight          = static_cast<int>(height);
    m_renderTargetWidth  = iOrigWidth;
    m_renderTargetHeight = iOrigHeight;

    // Signal the render thread to release and reacquire the GL context so WGL
    // re-evaluates the drawable surface dimensions against the new window size.
    m_pendingContextRebind.store(true);

    // Sync winMetrics so cameras, GUI, and FX all see a coherent coordinate space.
    winMetrics.x               = m_exclusiveMonitorLeft;
    winMetrics.y               = m_exclusiveMonitorTop;
    winMetrics.width           = iOrigWidth;
    winMetrics.height          = iOrigHeight;
    winMetrics.clientWidth     = iOrigWidth;
    winMetrics.clientHeight    = iOrigHeight;
    winMetrics.borderWidth     = 0;
    winMetrics.titleBarHeight  = 0;
    winMetrics.isFullScreen    = true;
    winMetrics.isBorderless    = false;
    winMetrics.monitorFullArea = { m_exclusiveMonitorLeft, m_exclusiveMonitorTop,
                                   m_exclusiveMonitorLeft + iOrigWidth,
                                   m_exclusiveMonitorTop  + iOrigHeight };
    winMetrics.monitorWorkArea = winMetrics.monitorFullArea;

    #if defined(_DEBUG_OPENGLRENDERER_) && defined(_DEBUG)
    {
        GLint vp[4] = {};
        glGetIntegerv(GL_VIEWPORT, vp);
        UINT dpi            = GetDpiForWindow(hWnd);
        RECT windowRect = {}, clientRect = {};
        GetWindowRect(hWnd, &windowRect);
        GetClientRect(hWnd, &clientRect);
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[OpenGLRenderer] Exclusive fullscreen diag: DPI=%u  Window=%dx%d  Client=%dx%d  "
            L"Config=%dx%d  RenderTarget=%dx%d  GLViewport=%dx%d",
            dpi,
            windowRect.right  - windowRect.left,
            windowRect.bottom - windowRect.top,
            clientRect.right  - clientRect.left,
            clientRect.bottom - clientRect.top,
            config.myConfig.resolutionWidth, config.myConfig.resolutionHeight,
            m_renderTargetWidth, m_renderTargetHeight,
            vp[2], vp[3]);
    }
    #endif

    threadManager.threadVars.bSettingFullScreen.store(false);
    return result;
}

bool OpenGLRenderer::SetFullScreen()
{
    HWND hWnd = m_glContext.windowHandle;

    // Save window state so SetWindowedScreen can restore the exact prior position and style.
    if (!m_savedWindowState.valid) {
        m_savedWindowState.style   = GetWindowLong(hWnd, GWL_STYLE);
        m_savedWindowState.exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
        GetWindowRect(hWnd, &m_savedWindowState.rect);
        m_savedWindowState.valid   = true;
    }

    HMONITOR hmon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hmon, &mi);
    int w = mi.rcMonitor.right  - mi.rcMonitor.left;
    int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

    // Do NOT clear GWL_EXSTYLE — flags like WS_EX_NOREDIRECTIONBITMAP affect presentation
    // mode and DPI handling; removing them causes offset/scaling artefacts.
    EnforceFullscreenWindowPlacement(mi.rcMonitor.left, mi.rcMonitor.top, w, h, L"borderless");

    IsWindowMode.store(false);
    iOrigWidth           = w;
    iOrigHeight          = h;
    m_renderTargetWidth  = w;
    m_renderTargetHeight = h;
    m_pendingContextRebind.store(true);

    winMetrics.x               = mi.rcMonitor.left;
    winMetrics.y               = mi.rcMonitor.top;
    winMetrics.width           = w;
    winMetrics.height          = h;
    winMetrics.clientWidth     = w;
    winMetrics.clientHeight    = h;
    winMetrics.borderWidth     = 0;
    winMetrics.titleBarHeight  = 0;
    winMetrics.isFullScreen    = false;
    winMetrics.isBorderless    = true;
    winMetrics.monitorFullArea = mi.rcMonitor;
    winMetrics.monitorWorkArea = mi.rcMonitor;
    return true;
}

void OpenGLRenderer::EnforceFullscreenWindowPlacement(int left, int top, int width, int height, const wchar_t* phase)
{
    HWND hWnd = m_glContext.windowHandle;
    if (!hWnd || width <= 0 || height <= 0) return;

    SetWindowLongW(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    ShowWindow(hWnd, SW_SHOWNORMAL);

    SetWindowPos(hWnd, HWND_TOPMOST,
        left, top, width, height,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

    BringWindowToTop(hWnd);
    SetActiveWindow(hWnd);
    SetFocus(hWnd);
    SetForegroundWindow(hWnd);

    SetWindowPos(hWnd, HWND_TOPMOST,
        left, top, width, height,
        SWP_SHOWWINDOW | SWP_NOOWNERZORDER);

    RECT wr = {}, cr = {};
    GetWindowRect(hWnd, &wr);
    GetClientRect(hWnd, &cr);
    LONG exStyle = GetWindowLongW(hWnd, GWL_EXSTYLE);
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"[OpenGLRenderer] Fullscreen HWND enforce (%s): Window=%ld,%ld %ldx%ld Client=%ldx%ld Topmost=%d ExStyle=0x%08lx",
        phase ? phase : L"unknown",
        wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
        cr.right - cr.left, cr.bottom - cr.top,
        (exStyle & WS_EX_TOPMOST) ? 1 : 0,
        static_cast<unsigned long>(exStyle));
}

float OpenGLRenderer::CalculateTextHeight(const std::wstring& text, float fontSize,
                                           float containerHeight)
{
    if (text.empty()) return fontSize * 1.5f;
    HDC dc = GetDC(nullptr);
    if (!dc) return fontSize * 1.5f;
    int ptSize = std::max(1, static_cast<int>(fontSize * 1.333f));
    HFONT hFont = CreateFontW(-ptSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    if (!hFont) { ReleaseDC(nullptr, dc); return fontSize * 1.5f; }
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, hFont));
    RECT rc = { 0, 0, 4096, 4096 };
    DrawTextW(dc, text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK);
    SelectObject(dc, old);
    DeleteObject(hFont);
    ReleaseDC(nullptr, dc);
    return static_cast<float>(rc.bottom + 2);  // +2 matches RenderTextToTexture padding
}

float OpenGLRenderer::CalculateTextWidth(const std::wstring& text, float fontSize,
                                          float containerWidth)
{
    if (text.empty()) return 0.0f;
    HDC dc = GetDC(nullptr);
    if (!dc) return static_cast<float>(text.size()) * fontSize * 0.6f;
    int ptSize = std::max(1, static_cast<int>(fontSize * 1.333f));
    HFONT hFont = CreateFontW(-ptSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    if (!hFont) { ReleaseDC(nullptr, dc); return static_cast<float>(text.size()) * fontSize * 0.6f; }
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, hFont));
    SIZE sz = {};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(dc, old);
    DeleteObject(hFont);
    ReleaseDC(nullptr, dc);
    return static_cast<float>(sz.cx);
}

float OpenGLRenderer::GetCharacterWidth(wchar_t character, float fontSize,
                                         const std::wstring& fontName)
{
    HDC dc = GetDC(nullptr);
    if (!dc) return fontSize * 0.6f;
    int ptSize = std::max(1, static_cast<int>(fontSize * 1.333f));
    HFONT hFont = CreateFontW(-ptSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        fontName.empty() ? L"Arial" : fontName.c_str());
    if (!hFont) { ReleaseDC(nullptr, dc); return fontSize * 0.6f; }
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, hFont));
    SIZE sz = {};
    GetTextExtentPoint32W(dc, &character, 1, &sz);
    SelectObject(dc, old);
    DeleteObject(hFont);
    ReleaseDC(nullptr, dc);
    return static_cast<float>(sz.cx);
}

float OpenGLRenderer::GetCharacterWidth(wchar_t character, float fontSize, bool bold)
{
    HDC dc = GetDC(nullptr);
    if (!dc) return fontSize * 0.6f;
    int ptSize = std::max(1, static_cast<int>(fontSize * 1.333f));
    HFONT hFont = CreateFontW(-ptSize, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    if (!hFont) { ReleaseDC(nullptr, dc); return fontSize * 0.6f; }
    HFONT old = reinterpret_cast<HFONT>(SelectObject(dc, hFont));
    SIZE sz = {};
    GetTextExtentPoint32W(dc, &character, 1, &sz);
    SelectObject(dc, old);
    DeleteObject(hFont);
    ReleaseDC(nullptr, dc);
    return static_cast<float>(sz.cx);
}

GLuint OpenGLRenderer::RenderTextCenteredToTexture(const std::wstring& text, float fontSize,
    const MyColor& color, int cw, int ch, int& outW, int& outH, bool bold)
{
    outW = cw; outH = ch;
    HDC memDC = CreateCompatibleDC(nullptr);
    if (!memDC) return 0;

    BITMAPINFO stubBmi{};
    stubBmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    stubBmi.bmiHeader.biWidth       = 1;
    stubBmi.bmiHeader.biHeight      = -1;
    stubBmi.bmiHeader.biPlanes      = 1;
    stubBmi.bmiHeader.biBitCount    = 32;
    stubBmi.bmiHeader.biCompression = BI_RGB;
    void* stubBits = nullptr;
    HBITMAP stubBmp = CreateDIBSection(memDC, &stubBmi, DIB_RGB_COLORS, &stubBits, nullptr, 0);
    HBITMAP preStub = stubBmp ? reinterpret_cast<HBITMAP>(SelectObject(memDC, stubBmp)) : nullptr;

    int ptSize = std::max(1, static_cast<int>(fontSize * 1.333f));
    HFONT hFont = CreateFontW(-ptSize, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    if (!hFont) {
        if (stubBmp) { SelectObject(memDC, preStub); DeleteObject(stubBmp); }
        DeleteDC(memDC); return 0;
    }
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(memDC, hFont));
    if (stubBmp) { SelectObject(memDC, preStub); DeleteObject(stubBmp); }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = cw;
    bmi.bmiHeader.biHeight      = -ch;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        SelectObject(memDC, oldFont); DeleteObject(hFont); DeleteDC(memDC); return 0;
    }
    HBITMAP oldBmp = reinterpret_cast<HBITMAP>(SelectObject(memDC, hBmp));
    MemoryZero(bits, static_cast<size_t>(cw) * ch << 2);   // hardware-bandwidth zero-fill; *4 -> <<2

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT draw = { 0, 0, cw, ch };
    DrawTextW(memDC, text.c_str(), -1, &draw, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    uint8_t* pixels = reinterpret_cast<uint8_t*>(bits);
    const int total = cw * ch;
    for (int i = 0; i < total; ++i) {
        ApplyGlyphColorAlpha(pixels + (i << 2), color);   // *4 -> <<2
    }

    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    SelectObject(memDC, oldBmp); SelectObject(memDC, oldFont);
    DeleteObject(hBmp); DeleteObject(hFont); DeleteDC(memDC);
    return texID;
}

void OpenGLRenderer::DrawMyTextStyled(const std::wstring& text, const Vector2& position,
    const MyColor& color, const TextRenderStyle& style)
{
    if (text.empty()) return;
    int tw = 0, th = 0;
    // Windows: GDI rasterise with full style attributes
    HDC memDC = CreateCompatibleDC(nullptr);
    if (!memDC) return;

    int ptSize = static_cast<int>(style.fontSize * 1.333f);
    HFONT hFont = CreateFontW(
        -ptSize, 0, 0, 0,
        style.bold        ? FW_BOLD    : FW_NORMAL,
        style.italic      ? TRUE       : FALSE,
        style.underline   ? TRUE       : FALSE,
        style.strikethrough ? TRUE     : FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        style.fontName.empty() ? L"Arial" : style.fontName.c_str());
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(memDC, hFont));

    RECT rc = { 0, 0, 4096, 4096 };
    DrawTextW(memDC, text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK);
    tw = rc.right + 2; th = rc.bottom + 2;

    if (tw > 0 && th > 0) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth    = tw;
        bmi.bmiHeader.biHeight   = -th;
        bmi.bmiHeader.biPlanes   = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HBITMAP oldBmp = reinterpret_cast<HBITMAP>(SelectObject(memDC, hBmp));

        RECT fill = { 0, 0, tw, th };
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &fill, black);
        DeleteObject(black);

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(255, 255, 255));   // always white — see RenderTextToTexture for rationale
        RECT draw = { 1, 1, tw - 1, th - 1 };
        DrawTextW(memDC, text.c_str(), -1, &draw, DT_WORDBREAK | DT_LEFT);

        uint8_t* pixels = reinterpret_cast<uint8_t*>(bits);
        for (int py = 0; py < th; ++py) {
            uint8_t* row = pixels + (static_cast<size_t>(py) * tw << 2);   // row offset hoisted out of inner loop; *4 -> <<2
            for (int px = 0; px < tw; ++px) {
                ApplyGlyphColorAlpha(row + (px << 2), color);
            }
        }

        GLuint texID = 0;
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Horizontal centering: when style.centered is set, ignore posX and derive it
        // from the render-target width so the text is centred across the full viewport
        // (mirrors DX11/Vulkan DrawMyTextStyled centring behaviour).
        int renderX = static_cast<int>(position.x);
        if (style.centered) {
            int rtW = m_renderTargetWidth > 0 ? m_renderTargetWidth : iOrigWidth;
            renderX = (rtW - tw) / 2;
        }

        Render2DQuad(texID,
            renderX, static_cast<int>(position.y),
            tw, th, 0, 0, tw, th, MyColor(255, 255, 255, 255), false);
        glDeleteTextures(1, &texID);

        SelectObject(memDC, oldBmp);
        DeleteObject(hBmp);
    }

    SelectObject(memDC, oldFont);
    DeleteObject(hFont);
    DeleteDC(memDC);
}

GLuint OpenGLRenderer::RenderTextToTexture(const std::wstring& text,
    const std::wstring& fontName, float fontSize,
    const MyColor& color, int& outW, int& outH)
{
    // Use GDI to measure and rasterise text
    HDC memDC = CreateCompatibleDC(nullptr);
    if (!memDC) return 0;

    // Root cause of AV: CreateCompatibleDC(nullptr) starts with a 1×1 MONOCHROME
    // (1 BPP) bitmap selected.  DrawTextW with any antialiased font on a 1 BPP DC
    // crashes inside USER32.dll because GDI's antialiasing path reads the DC bit depth,
    // finds 1, and faults accessing subpixel-rendering tables that don't exist for
    // monochrome surfaces.  GetDC(nullptr) (used by CalculateTextWidth) returns the
    // 32-bit screen DC, which is why those calls never crash.
    //
    // Fix: select a throwaway 1×1 32-bit DIB into the DC BEFORE font selection and
    // measurement, making it a colour-capable DC throughout.  Swap the real bitmap in
    // after measurement.

    // --- Throwaway 32-bit colour surface (makes DC colour-capable before font select) ---
    BITMAPINFO stubBmi{};
    stubBmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    stubBmi.bmiHeader.biWidth       = 1;
    stubBmi.bmiHeader.biHeight      = -1;
    stubBmi.bmiHeader.biPlanes      = 1;
    stubBmi.bmiHeader.biBitCount    = 32;
    stubBmi.bmiHeader.biCompression = BI_RGB;
    void* stubBits = nullptr;
    HBITMAP stubBmp = CreateDIBSection(memDC, &stubBmi, DIB_RGB_COLORS, &stubBits, nullptr, 0);
    HBITMAP preMeasBmp = stubBmp
        ? reinterpret_cast<HBITMAP>(SelectObject(memDC, stubBmp))
        : nullptr;

    int ptSize = std::max(1, static_cast<int>(fontSize * 1.333f));
    HFONT hFont = CreateFontW(-ptSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        fontName.empty() ? L"Arial" : fontName.c_str());
    if (!hFont) {
        if (stubBmp) { SelectObject(memDC, preMeasBmp); DeleteObject(stubBmp); }
        DeleteDC(memDC); return 0;
    }
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(memDC, hFont));

    // Measure text — safe now; the DC is 32 BPP
    RECT rc = { 0, 0, 4096, 4096 };
    DrawTextW(memDC, text.c_str(), -1, &rc, DT_CALCRECT | DT_WORDBREAK);
    outW = rc.right  + 2;
    outH = rc.bottom + 2;

    // Swap out the stub bitmap; deselect before deleting
    if (stubBmp) { SelectObject(memDC, preMeasBmp); DeleteObject(stubBmp); }

    if (outW <= 2 || outH <= 2) {
        SelectObject(memDC, oldFont); DeleteObject(hFont); DeleteDC(memDC);
        return 0;
    }

    // Create the real RGBA render bitmap
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = outW;
    bmi.bmiHeader.biHeight      = -outH;  // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        SelectObject(memDC, oldFont); DeleteObject(hFont); DeleteDC(memDC);
        return 0;
    }
    HBITMAP oldBmp = reinterpret_cast<HBITMAP>(SelectObject(memDC, hBmp));

    // Fill black background (transparent)
    RECT fill = { 0, 0, outW, outH };
    HBRUSH black = CreateSolidBrush(RGB(0,0,0));
    FillRect(memDC, &fill, black);
    DeleteObject(black);

    // Draw text — always rasterize in WHITE so (r+g+b)/3 yields correct luminance-as-alpha
    // for every requested color.  Dark/black text rasterized in its own color produces
    // luminance=0 → alpha=0 everywhere (invisible).  We override the pixel RGB to the
    // requested color AFTER extraction so the final texture carries the right color AND
    // the right alpha regardless of how dark the color is.
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));   // always white in GDI
    RECT draw = { 1, 1, outW - 1, outH - 1 };
    DrawTextW(memDC, text.c_str(), -1, &draw, DT_WORDBREAK | DT_LEFT);

    // Convert BGRA→RGBA: use white-glyph luminance as alpha, override RGB with requested color.
    uint8_t* pixels = reinterpret_cast<uint8_t*>(bits);
    for (int py = 0; py < outH; ++py) {
        uint8_t* row = pixels + (static_cast<size_t>(py) * outW << 2);   // row offset hoisted out of inner loop; *4 -> <<2
        for (int px = 0; px < outW; ++px) {
            ApplyGlyphColorAlpha(row + (px << 2), color);
        }
    }

    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, outW, outH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    SelectObject(memDC, oldBmp); SelectObject(memDC, oldFont);
    DeleteObject(hBmp); DeleteObject(hFont); DeleteDC(memDC);
    return texID;
}

bool OpenGLRenderer::LoadTextureFromFilePlatform(const std::wstring& filename, bool is2D, OpenGLTexture& slot)
{
    // ── Windows path: GDI+ decode ──────────────────────────────────────────
    Gdiplus::Bitmap bmp(filename.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[OpenGLRenderer] LoadTexture: GDI+ cannot open: " + filename);
        return false;
    }

    GLsizei W = static_cast<GLsizei>(bmp.GetWidth());
    GLsizei H = static_cast<GLsizei>(bmp.GetHeight());
    if (W == 0 || H == 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[OpenGLRenderer] LoadTexture: zero-sized image: " + filename);
        return false;
    }

    Gdiplus::BitmapData bd;
    Gdiplus::Rect rect(0, 0, W, H);
    // PixelFormat32bppARGB stores bytes as B,G,R,A in memory (little-endian).
    // GL_BGRA maps this directly: no swizzle needed, stored as RGBA8 internally.
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[OpenGLRenderer] LoadTexture: GDI+ LockBits failed: " + filename);
        return false;
    }

    glGenTextures(1, &slot.textureID);
    if (!slot.textureID) {
        bmp.UnlockBits(&bd);
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[OpenGLRenderer] LoadTexture: glGenTextures returned 0 (GL context not current?): " + filename);
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, slot.textureID);

    // Upload pixel data. GL_BGRA is OpenGL 1.2+ standard; same value as GL_BGRA_EXT.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, bd.Scan0);
    bmp.UnlockBits(&bd);

    if (is2D) {
        // 2D UI textures — no mipmaps. glGenerateMipmap on the shared loader
        // context races with the render thread sampling the same texture on the
        // main context (mipmap levels 1+ being written while the render thread
        // samples them via GL_LINEAR_MIPMAP_LINEAR → nvoglv64 access violation).
        // UI textures are always rendered at or near native resolution so
        // GL_LINEAR is identical in quality and avoids the race entirely.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        // 3D scene textures — mipmaps needed for textures at grazing angles.
        // Mipmap generation is safe here because 3D textures are only used by
        // the render thread after bLoaderTaskFinished is true (loader is paused).
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        // Anisotropic filtering for 3D textures viewed at grazing angles
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        if (maxAniso > 1.0f)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
    }

    glBindTexture(GL_TEXTURE_2D, 0);

    // Verify no GL error occurred during upload
    GLenum glErr = glGetError();
    if (glErr != GL_NO_ERROR) {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[OpenGLRenderer] LoadTexture: GL error 0x" + std::to_wstring(glErr) +
            L" uploading: " + filename);
        glDeleteTextures(1, &slot.textureID);
        slot.textureID = 0;
        return false;
    }

    slot.width    = W;
    slot.height   = H;
    slot.format   = GL_RGBA8;
    slot.target   = GL_TEXTURE_2D;
    slot.isLoaded = true;
    return true;

}

void OpenGLRenderer::ShutdownPlatformTextBackend()
{
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
}

void OpenGLRenderer::RestorePlatformDisplayMode()
{
    if (m_isExclusiveFullscreen) {
        const wchar_t* dev = m_exclusiveMonitorDevice.empty() ? nullptr : m_exclusiveMonitorDevice.c_str();
        ChangeDisplaySettingsExW(dev, nullptr, nullptr, 0, nullptr);
        m_isExclusiveFullscreen = false;
        m_exclusiveMonitorDevice.clear();
    }
}

void OpenGLRenderer::CleanupPlatformSpecificContext()
{
    if (m_loaderGLContext) {
        wglDeleteContext(m_loaderGLContext);
        m_loaderGLContext = nullptr;
    }
    if (m_glContext.renderingContext) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_glContext.renderingContext);
        m_glContext.renderingContext = nullptr;
    }
    if (m_glContext.deviceContext && m_glContext.windowHandle) {
        ReleaseDC(m_glContext.windowHandle, m_glContext.deviceContext);
        m_glContext.deviceContext = nullptr;
    }
}

void OpenGLRenderer::SetupPlatformSpecificContext()
{
    // Platform-specific post-context setup (vsync, debug output, etc.)
    if (WGLEW_EXT_swap_control)
        wglSwapIntervalEXT(config.myConfig.enableVSync ? 1 : 0);
}

bool OpenGLRenderer::InitializeOpenGLExtensions()
{
    // GLEW was already initialised in CreateOpenGLContext; just verify it
    if (!GLEW_VERSION_3_3) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[OpenGLRenderer] OpenGL 3.3 not supported");
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform-specific OpenGL context creation (Windows WGL)
// ─────────────────────────────────────────────────────────────────────────────
bool OpenGLRenderer::CreateOpenGLContext(NativeWindowHandle hWnd)
{
    m_glContext.windowHandle = hWnd;
    m_glContext.deviceContext = GetDC(hWnd);
    if (!m_glContext.deviceContext) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] GetDC failed");
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;
    m_glContext.pixelFormatDescriptor = pfd;

    // MSAA: a window's pixel format can only be set once and wglChoosePixelFormatARB needs a live GL context,
    // so a hidden throw-away window is used to find a multisampled format first (0 = none / MSAA off).
    int pixelFormat = 0;
    if (config.myConfig.antiAliasingEnabled && config.myConfig.msaaEnabled)
        pixelFormat = FindMultisamplePixelFormat(m_glContext.deviceContext, pfd);

    if (pixelFormat)
    {
        DescribePixelFormat(m_glContext.deviceContext, pixelFormat, sizeof(pfd), &pfd);
        m_glContext.pixelFormatDescriptor = pfd;
        if (!SetPixelFormat(m_glContext.deviceContext, pixelFormat, &pfd)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] SetPixelFormat (multisample) failed");
            return false;
        }
    }
    else
    {
        pixelFormat = ChoosePixelFormat(m_glContext.deviceContext, &pfd);
        if (!pixelFormat || !SetPixelFormat(m_glContext.deviceContext, pixelFormat, &pfd)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] SetPixelFormat failed");
            return false;
        }
    }

    // Create a legacy context first to bootstrap WGL extensions
    HGLRC tempCtx = wglCreateContext(m_glContext.deviceContext);
    if (!tempCtx) { debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] wglCreateContext (legacy) failed"); return false; }
    wglMakeCurrent(m_glContext.deviceContext, tempCtx);

    // Now init GLEW so we can use wglCreateContextAttribsARB
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] GLEW init failed");
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tempCtx);
        return false;
    }

    if (WGLEW_ARB_create_context) {
        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            #if defined(_DEBUG)
                WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
            #endif
            0
        };
        m_glContext.renderingContext = wglCreateContextAttribsARB(m_glContext.deviceContext, nullptr, attribs);
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tempCtx);
        if (!m_glContext.renderingContext) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[OpenGLRenderer] Core 3.3 context creation failed");
            return false;
        }
        wglMakeCurrent(m_glContext.deviceContext, m_glContext.renderingContext);
    } else {
        // Fall back to legacy context
        m_glContext.renderingContext = tempCtx;
    }

    // Create a shared context for the loader thread so it can upload textures
    // concurrently with the render thread (shared contexts share all GL objects).
    if (WGLEW_ARB_create_context && m_glContext.renderingContext) {
        int loaderAttribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0
        };
        m_loaderGLContext = wglCreateContextAttribsARB(
            m_glContext.deviceContext, m_glContext.renderingContext, loaderAttribs);
        if (!m_loaderGLContext)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[OpenGLRenderer] Shared loader context creation failed — textures must load on render thread");
        else
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[OpenGLRenderer] Shared loader context created");
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[OpenGLRenderer] OpenGL 3.3 Core context created");
    return true;
}

// Finds a double-buffered 32-bit colour / 24-bit depth / 8-bit stencil pixel format with the largest
// sample count <= config.msaaSamples (2/4/8).  Returns 0 when WGL_ARB_pixel_format / multisample are
// unavailable or nothing matches; the caller then falls back to the plain ChoosePixelFormat path.
// Uses a hidden window + legacy context only to load the WGL extension entry points.
int OpenGLRenderer::FindMultisamplePixelFormat(HDC realDC, const PIXELFORMATDESCRIPTOR& basePfd)
{
    (void)realDC;
    int result = 0;

    HWND dummyWnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!dummyWnd) return 0;
    HDC dummyDC = GetDC(dummyWnd);
    HGLRC dummyRC = nullptr;

    // Save whatever context is current (none at this point in startup, but stay safe)
    HDC   prevDC = wglGetCurrentDC();
    HGLRC prevRC = wglGetCurrentContext();

    if (dummyDC)
    {
        PIXELFORMATDESCRIPTOR pfd = basePfd;
        const int fmt = ChoosePixelFormat(dummyDC, &pfd);
        if (fmt && SetPixelFormat(dummyDC, fmt, &pfd))
            dummyRC = wglCreateContext(dummyDC);
    }

    if (dummyRC && wglMakeCurrent(dummyDC, dummyRC))
    {
        if (glewInit() == GLEW_OK && WGLEW_ARB_pixel_format && WGLEW_ARB_multisample)
        {
            const int wantSamples = std::clamp(config.myConfig.msaaSamples, 2, 8);
            for (int samples = wantSamples; samples >= 2 && !result; samples >>= 1)
            {
                const int attribs[] = {
                    WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
                    WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
                    WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
                    WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
                    WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
                    WGL_COLOR_BITS_ARB,     32,
                    WGL_DEPTH_BITS_ARB,     24,
                    WGL_STENCIL_BITS_ARB,   8,
                    WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
                    WGL_SAMPLES_ARB,        samples,
                    0
                };
                int  fmt = 0;
                UINT count = 0;
                if (wglChoosePixelFormatARB(dummyDC, attribs, nullptr, 1, &fmt, &count) && count > 0 && fmt > 0)
                {
                    result = fmt;
                    m_msaaSampleCount = samples;
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"[OpenGLRenderer] Multisample pixel format found: %dx (requested %dx)", samples, wantSamples);
                }
            }
            if (!result)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[OpenGLRenderer] MSAA requested but no multisample pixel format is available; rendering 1 sample.");
        }
        else
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[OpenGLRenderer] MSAA requested but WGL_ARB_multisample / pixel_format are unsupported; rendering 1 sample.");
        }
        wglMakeCurrent(nullptr, nullptr);
    }

    if (prevDC && prevRC) wglMakeCurrent(prevDC, prevRC);
    if (dummyRC) wglDeleteContext(dummyRC);
    if (dummyDC) ReleaseDC(dummyWnd, dummyDC);
    DestroyWindow(dummyWnd);
    return result;
}

void OpenGLRenderer::ReleasePlatformContext()
{
    // Transfer GL context ownership from main thread to render thread.
    // fxManager.Initialize() has already compiled its shaders on the main thread.
    wglMakeCurrent(nullptr, nullptr);
}

void OpenGLRenderer::DetectPlatformGPUCaps()
{
    // Enumerate the default adapter via DXGI to get VRAM figures.
    IDXGIFactory* dxgiFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&dxgiFactory)))
    {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgiFactory->EnumAdapters(0, &adapter)))
        {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                m_dedicatedVRAMMB   = static_cast<UINT64>(desc.DedicatedVideoMemory / (1024 * 1024));
                m_sharedSystemMemMB = static_cast<UINT64>(desc.SharedSystemMemory   / (1024 * 1024));
            }
            adapter->Release();
        }
        dxgiFactory->Release();
    }
}

void OpenGLRenderer::InitPlatformTextBackend()
{
    // GDI+ must be started before any LoadTexture call (Gdiplus::Bitmap requires it)
    if (!m_gdiplusToken) {
        Gdiplus::GdiplusStartupInput gdiplusInput;
        Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusInput, nullptr);
    }
}

// Static helper moved from OpenGLRenderer.cpp with the text-rasterisation
// functions that use it (RenderTextToTexture, RenderTextCenteredToTexture,
// DrawMyTextStyled). Converts one BGRA glyph pixel in place to the requested
// RGBA color, using the source luminance as the alpha channel.
static inline void ApplyGlyphColorAlpha(uint8_t* pixel, const MyColor& color)
{
    const uint8_t luminance = static_cast<uint8_t>((pixel[2] + pixel[1] + pixel[0]) / 3);
    pixel[0] = color.r;
    pixel[1] = color.g;
    pixel[2] = color.b;
    pixel[3] = static_cast<uint8_t>((static_cast<uint32_t>(luminance) * color.a) / 255);
}

void OpenGLRenderer::PresentPlatformFrame()
{
    // Apply per-frame VSync setting — update swap interval if config changed
    static bool lastVSync = true;
    bool vsyncNow = config.myConfig.enableVSync;
    if (vsyncNow != lastVSync) {
        if (WGLEW_EXT_swap_control)
            wglSwapIntervalEXT(vsyncNow ? 1 : 0);
        lastVSync = vsyncNow;
    }

    SwapBuffers(m_glContext.deviceContext);

}

void OpenGLRenderer::RebindPlatformContext()
{
    if (m_pendingContextRebind.exchange(false)) {
        wglMakeCurrent(nullptr, nullptr);
        wglMakeCurrent(m_glContext.deviceContext, m_glContext.renderingContext);
    }
}

bool OpenGLRenderer::MakePlatformContextCurrent()
{
    if (!wglMakeCurrent(m_glContext.deviceContext, m_glContext.renderingContext)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] wglMakeCurrent failed — render thread cannot acquire GL context");
        return false;
    }
    return true;
}

bool OpenGLRenderer::HasPlatformGLContext() const
{
    return m_glContext.renderingContext != nullptr;
}

void OpenGLRenderer::DrawIntroMovieFrame()
{
    // Retrieve current RGBA frame from MoviePlayer and upload / update a GL texture
    uint32_t fw = 0, fh = 0;
    const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
    if (frameData && fw > 0 && fh > 0) {
        static GLuint movieTexID = 0;
        static uint32_t lastW = 0, lastH = 0;

        if (!movieTexID) glGenTextures(1, &movieTexID);
        glBindTexture(GL_TEXTURE_2D, movieTexID);
        // CPU buffer is RGBA (MoviePlayer::UpdateVideoTextureCPU swaps BGRA→RGBA for OpenGL)
        if (fw != lastW || fh != lastH) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameData);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            lastW = fw; lastH = fh;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, frameData);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        // Blit movie to fill entire screen
        DrawVideoFrame(
            Vector2(0.0f, 0.0f),
            Vector2(static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight)),
            MyColor(255, 255, 255, 255),
            movieTexID);
    }

    // Company logo overlay: half size, bottom-left corner
    {
        int logoIdx = int(BlitObj2DIndexType::IMG_COMPANYLOGO);
        if (logoIdx >= 0 && logoIdx < MAX_TEXTURE_BUFFERS && m_2dTextures[logoIdx].isLoaded) {
            int halfW = m_2dTextures[logoIdx].width  / 2;
            int halfH = m_2dTextures[logoIdx].height / 2;
            if (fxManager.IsImageZoomActive(logoIdx))
                fxManager.RenderZoomedImage(logoIdx, 0, iOrigHeight - halfH, halfW, halfH);
            else
                Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
        }
    }

    // Spacebar skip — identical across all four renderers: the movie keeps playing
    // through the fade (no jarring freeze-frame cut) and is only stopped once
    // FadeOutThenCallback's own completion callback fires, so the fade duration
    // (1.0s) is the single source of truth instead of an immediate hard cut.
    // Only active in SCENE_INTRO_MOVIE, not the splash SCENE_INTRO.
    if (scene.stSceneType == SceneType::SCENE_INTRO_MOVIE && KeyboardHandler::GetInstance().IsPlatformKeyPressed(' ') && !scene.bSceneSwitching)
    {
        scene.bSceneSwitching = true;
        fxManager.FadeOutThenCallback(XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f), 1.0f, 0.06f, []() {
            moviePlayer.Stop();
        });
    }
}

// Small GL helpers factored out of shared functions (platform split, 24 Nov 2026;
// Phase D EGL work) — the desktop enums they use do not exist in OpenGL ES.
void OpenGLRenderer::SetPlatformLineSmoothing(bool enable)
{
    if (enable) glEnable(GL_LINE_SMOOTH); else glDisable(GL_LINE_SMOOTH);
}

void OpenGLRenderer::SetPlatformMultisampling(bool enable)
{
    if (enable) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
}

void OpenGLRenderer::SetupPlatformShadowTextureParams(GLenum target, const float border[4])
{
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, border);
}

void OpenGLRenderer::SetPlatformDrawBufferNone()
{
    glDrawBuffer(GL_NONE);
}

#endif // __USE_OPENGL__
