#pragma once

#define NOMINMAN

#include "Includes.h"
#include "ConsoleWindow.h"
#include "GUIManager.h"
#include "Renderer.h"
#include "Color.h"
#include "Vectors.h"
#include "Debug.h"
#include "ThreadManager.h"

extern Debug         debug;
extern ThreadManager threadManager;

// Global instance — referenced as extern ConsoleWindow consoleWindow in consuming files.
ConsoleWindow consoleWindow;

// ---------------------------------------------------------------------------
// CreateInGUIManager
// ---------------------------------------------------------------------------
// Creates the console as a proper GUIManager window and registers all rendering
// and input callbacks.  The window starts hidden (isVisible = false).
// Call once after GUIManager::Initialize(); Toggle() shows it thereafter.
void ConsoleWindow::CreateInGUIManager(GUIManager& gm)
{
    m_guiMgr         = &gm;
    m_lastCursorFlip = std::chrono::steady_clock::now();

    // Window uses an invisible background — RenderContent draws everything itself.
    gm.CreateMyWindow("ConsoleWindow",
                      GUIWindowType::Standard,
                      Vector2(CONSOLE_WIN_X, CONSOLE_WIN_Y),
                      Vector2(CONSOLE_WIN_W_MIN, 600.0f),   // placeholder; corrected first Render
                      MyColor(0, 0, 0, 0),
                      -1);

    auto win = gm.GetWindow("ConsoleWindow");
    if (!win) {
        debug.LogError("ConsoleWindow::CreateInGUIManager - GUIManager window creation failed.");
        return;
    }

    // Hide immediately; Toggle() makes it visible when needed.
    win->isVisible = false;
    m_guiWin = win;

    // --- Wire all callbacks ---

    win->onCustomRender = [this](Renderer* r) {
        RenderContent(r);
    };

    win->onCustomMouseInput = [this](float x, float y) {
        HandleScrollbarClick(x, y);
    };

    win->onCharInput = [this](wchar_t c) {
        OnCharInput(c);
    };

    win->onBackspace = [this]() {
        OnBackspace();
    };

    win->onEnter = [this]() {
        OnEnter();
    };

    win->onMouseWheel = [this](int delta) {
        OnMouseWheel(delta);
    };
}

// ---------------------------------------------------------------------------
// Toggle
// ---------------------------------------------------------------------------
void ConsoleWindow::Toggle()
{
    bIsVisible = !bIsVisible;

    if (m_guiMgr) {
        // When showing, immediately correct the GUIWindow bounds from the renderer
        // so hit-testing is accurate on the very first input frame after Toggle().
        // (RenderContent also updates bounds each render frame, but the first
        //  HandleAllInput call may arrive before the first render if the user
        //  clicks immediately after pressing F8.)
        if (bIsVisible) {
            // Reset cursor to visible so it is always on when the console opens.
            m_cursorVisible  = true;
            m_lastCursorFlip = std::chrono::steady_clock::now();

            if (auto win = m_guiWin.lock()) {
                if (win->myRenderer) {
                    Renderer* r = win->myRenderer;
                    win->position = Vector2(CONSOLE_WIN_X, CONSOLE_WIN_Y);
                    win->size.x   = std::clamp(static_cast<float>(r->iOrigWidth)  * CONSOLE_WIN_W_PCT,
                                               CONSOLE_WIN_W_MIN, CONSOLE_WIN_W_MAX);
                    win->size.y   = static_cast<float>(r->iOrigHeight) - CONSOLE_WIN_H_MARGIN;
                }
            }
        }

        m_guiMgr->SetWindowVisibility("ConsoleWindow", bIsVisible);
        if (bIsVisible)
            m_guiMgr->BringWindowToFront("ConsoleWindow");
    }
}

// ---------------------------------------------------------------------------
// AddLine  (thread-safe)
// ---------------------------------------------------------------------------
void ConsoleWindow::AddLine(const std::wstring& line, ConsoleLineColor type)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.push_back({line, type});
    while (static_cast<int>(m_buffer.size()) > CONSOLE_MAX_LINES)
        m_buffer.pop_front();
    m_scrollOffset = 0;
}

// ---------------------------------------------------------------------------
// Scroll
// ---------------------------------------------------------------------------
void ConsoleWindow::Scroll(int delta)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const int totalLines = static_cast<int>(m_buffer.size());
    const int maxOffset  = std::max(0, totalLines - m_visibleLines.load());
    m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxOffset);
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------
void ConsoleWindow::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
    m_scrollOffset = 0;
}

// ---------------------------------------------------------------------------
// SetCommandCallback
// ---------------------------------------------------------------------------
void ConsoleWindow::SetCommandCallback(std::function<void(const std::wstring&)> cb)
{
    m_commandCallback = std::move(cb);
}

// ---------------------------------------------------------------------------
// Private keyboard / wheel callbacks
// ---------------------------------------------------------------------------
void ConsoleWindow::OnCharInput(wchar_t c)
{
    if (!bIsVisible) return;
    if (c >= 32 && c != 127) {
        m_cmdLine       += c;
        // Keep cursor visible while the user is actively typing.
        m_cursorVisible  = true;
        m_lastCursorFlip = std::chrono::steady_clock::now();
    }
}

void ConsoleWindow::OnBackspace()
{
    if (!bIsVisible || m_cmdLine.empty()) return;
    m_cmdLine.pop_back();
    // Keep cursor visible while the user is actively editing.
    m_cursorVisible  = true;
    m_lastCursorFlip = std::chrono::steady_clock::now();
}

void ConsoleWindow::OnEnter()
{
    if (!bIsVisible || m_cmdLine.empty()) return;
    AddLine(L"> " + m_cmdLine);
    ProcessCommand(m_cmdLine);
    m_cmdLine.clear();
}

void ConsoleWindow::OnMouseWheel(int delta)
{
    if (!bIsVisible) return;
    Scroll(delta > 0 ? 3 : -3);
}

// ---------------------------------------------------------------------------
// ProcessCommand
// ---------------------------------------------------------------------------
// Dispatches the submitted command line.  Built-in commands are handled first
// (case-insensitive, leading/trailing whitespace is stripped).  If the input
// does not match a built-in the external m_commandCallback receives it.  When
// no callback is registered and the command is unknown, an error line is
// printed to the buffer.
void ConsoleWindow::ProcessCommand(const std::wstring& raw)
{
    // Strip leading and trailing whitespace.
    const size_t first = raw.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return;                // blank / whitespace-only
    const size_t last    = raw.find_last_not_of(L" \t");
    const std::wstring trimmed = raw.substr(first, last - first + 1);

    // Lowercase copy for case-insensitive matching.
    std::wstring lower = trimmed;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    // --- Built-in: clear ---
    if (lower == L"clear") {
        Clear();
        return;
    }

    // --- Built-in: close — hide the console ---
    if (lower == L"close") {
        Toggle();
        return;
    }

    // --- Built-in: top — scroll to oldest (top) of buffer ---
    if (lower == L"top") {
        std::lock_guard<std::mutex> lock(m_mutex);
        const int totalLines = static_cast<int>(m_buffer.size());
        m_scrollOffset = std::max(0, totalLines - m_visibleLines.load());
        return;
    }

    // -----------------------------------------------------------------------
    // DEBUG-only built-in commands — stripped from Release builds entirely.
    // -----------------------------------------------------------------------
#if defined(_DEBUG)
    // --- Built-in (DEBUG): test load dialog ---
    // Creates a GUIWindows Load Dialog and verifies it can be opened and closed.
    if (lower == L"test load dialog") {
        if (!m_guiMgr) {
            AddLine(L"test load dialog: GUIManager is not available.", ConsoleLineColor::Warning);
            return;
        }
        m_guiMgr->CreateLoadDialog(
            L"Test Load Dialog",
            L"C:\\",
            { { L"All Files (*.*)", L"*.*" } },
            [this](const std::wstring& path) {
                AddLine(L"Load dialog confirmed: " + path);
            },
            [this]() {
                AddLine(L"Load dialog cancelled.");
            });
        return;
    }

    // --- Built-in (DEBUG): test save dialog ---
    // Creates a GUIWindows Save Dialog and verifies it can be opened and closed.
    if (lower == L"test save dialog") {
        if (!m_guiMgr) {
            AddLine(L"test save dialog: GUIManager is not available.", ConsoleLineColor::Warning);
            return;
        }
        m_guiMgr->CreateSaveDialog(
            L"Test Save Dialog",
            L"C:\\",
            L"untitled.txt",
            { { L"All Files (*.*)", L"*.*" } },
            [this](const std::wstring& path) {
                AddLine(L"Save dialog confirmed: " + path);
            },
            [this]() {
                AddLine(L"Save dialog cancelled.");
            });
        return;
    }

    // --- Built-in (DEBUG): help ---
    // Loads help.dat, dumps its contents line-by-line to the console, then
    // explicitly releases the read buffer.
    if (lower == L"help") {
        FILE* fp = nullptr;
        #if defined(PLATFORM_WINDOWS)
            fopen_s(&fp, "help.dat", "rb");
        #else
            fp = fopen("help.dat", "rb");
        #endif
        if (!fp) {
            AddLine(L"help: could not open help.dat", ConsoleLineColor::Error);
            return;
        }

        fseek(fp, 0, SEEK_END);
        const long fileSize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (fileSize <= 0) {
            fclose(fp);
            AddLine(L"help: help.dat is empty.", ConsoleLineColor::Warning);
            return;
        }

        // Allocate raw buffer — must be explicitly released after use.
        char* buf = new char[static_cast<size_t>(fileSize) + 1];
        fread(buf, 1, static_cast<size_t>(fileSize), fp);
        fclose(fp);
        buf[fileSize] = '\0';

        // Walk the buffer and emit one console line per text line.
        const char* p   = buf;
        const char* end = buf + fileSize;
        while (p < end) {
            // Locate next newline or end of buffer.
            const char* nl = p;
            while (nl < end && *nl != '\n') ++nl;

            // Build the narrow line, strip any trailing carriage return.
            std::string narrow(p, nl);
            if (!narrow.empty() && narrow.back() == '\r')
                narrow.pop_back();

            // Narrow-to-wide: valid for ASCII / Latin-1 help text.
            AddLine(std::wstring(narrow.begin(), narrow.end()));

            p = (nl < end) ? nl + 1 : end;
        }

        // Explicitly release the read buffer.
        delete[] buf;
        buf = nullptr;

        return;
    }
#endif  // _DEBUG

    // --- External command handler ---
    if (m_commandCallback) {
        m_commandCallback(trimmed);
        return;
    }

    // Unknown command — no handler registered and not a built-in.
    AddLine(L"> Command [" + trimmed + L"] is unknown.", ConsoleLineColor::Warning);
}

// ---------------------------------------------------------------------------
// HandleScrollbarClick  (onCustomMouseInput callback)
// ---------------------------------------------------------------------------
void ConsoleWindow::HandleScrollbarClick(float x, float y)
{
    if (!bIsVisible || m_sbH <= 0.0f || m_knobH <= 0.0f) return;

    // X: expand hit area by 8px on each side so the narrow scrollbar is easy to grab.
    constexpr float kXTolerance = 8.0f;
    if (x < m_sbX - kXTolerance || x > m_sbX + CONSOLE_SCROLLBAR_W + kXTolerance) return;

    // Y: accept only the knob (thumb) area so dragging behaves predictably.
    constexpr float kYTolerance = 4.0f;
    if (y < m_knobY - kYTolerance || y > m_knobY + m_knobH + kYTolerance) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    const int totalLines = static_cast<int>(m_buffer.size());
    const int maxOffset  = std::max(0, totalLines - m_visibleLines.load());
    if (maxOffset == 0) return;

    // Map the click position within the track to a scroll offset.
    // top of track = maxOffset (oldest lines); bottom = 0 (newest lines).
    const float t  = 1.0f - (y - m_sbY) / m_sbH;
    m_scrollOffset = std::clamp(static_cast<int>(t * static_cast<float>(maxOffset) + 0.5f), 0, maxOffset);
}

// ---------------------------------------------------------------------------
// RenderContent  (onCustomRender callback)
// ---------------------------------------------------------------------------
// Draws the full console overlay — border, title bar, text area, scrollbar,
// and command-line bar.  Also keeps the GUIWindow bounds up to date each
// frame so focus and hit-testing always match what is visually drawn.
void ConsoleWindow::RenderContent(Renderer* r)
{
    if (!bIsVisible || !r) return;
    if (!r->bIsInitialized.load()) return;
    if (threadManager.threadVars.bIsShuttingDown.load() ||
        threadManager.threadVars.bIsResizing.load()) return;

    // Cursor blink — flip visibility every 1 second.
    {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastCursorFlip).count();
        if (elapsed >= 1000) {
            m_cursorVisible  = !m_cursorVisible;
            m_lastCursorFlip = now;
        }
    }

    // Font size scaled to screen height; ~10pt at 1080p, clamped to [8, 12].
    // OpenGL uses GDI which renders ~10% wider than DWrite; a slightly smaller
    // font (÷120 instead of ÷108) keeps lines from reaching the scrollbar.
    #if defined(__USE_OPENGL__)
        const float fontSize   = std::clamp(static_cast<float>(r->iOrigHeight) / 120.0f, 7.0f, 12.0f);
    #else
        const float fontSize   = std::clamp(static_cast<float>(r->iOrigHeight) / 108.0f, 8.0f, 14.0f);
    #endif
    const float lineHeight = fontSize + 4.0f;

    // Window geometry — computed from current screen size each frame.
    const float winW     = std::clamp(static_cast<float>(r->iOrigWidth)  * CONSOLE_WIN_W_PCT,
                                      CONSOLE_WIN_W_MIN, CONSOLE_WIN_W_MAX);
    const float winH     = static_cast<float>(r->iOrigHeight) - CONSOLE_WIN_H_MARGIN;
    const float winX     = CONSOLE_WIN_X;
    const float winY     = CONSOLE_WIN_Y;
    const float contentH  = winH - CONSOLE_TITLEBAR_H;
    const float textAreaH = contentH - CONSOLE_CMDBAR_H - 1.0f;
    const int   visibleLines = std::max(1, static_cast<int>((textAreaH - CONSOLE_PADDING * 2.0f) / lineHeight));
    m_visibleLines.store(visibleLines);

    // Keep the GUIWindow bounds in sync so hit-testing matches the visual draw.
    if (auto win = m_guiWin.lock()) {
        win->position = Vector2(winX, winY);
        win->size     = Vector2(winW, winH);
    }

    // -----------------------------------------------------------------------
    // 3D panel helpers — matching GUIManager Panel raised/sunken bevel style.
    // -----------------------------------------------------------------------
    // Raised: drop shadow behind, outer bevel top/left bright bottom/right dark,
    //         inner bevel same logic at half-alpha for double depth.
    // Sunken: no shadow, reversed bevel directions.
    auto DrawRaisedPanel = [&](float x, float y, float w, float h, MyColor bg) {
        r->DrawRectangle(Vector2(x + 3.0f, y + 3.0f), Vector2(w, h),
                         MyColor(0, 0, 0, 85), true);
        r->DrawRectangle(Vector2(x, y), Vector2(w, h), bg, true);
        const MyColor oH(88, 94, 118, 220), oS(8, 10, 18, 220);
        r->DrawRectangle(Vector2(x,       y),         Vector2(w, 1.0f), oH, true);
        r->DrawRectangle(Vector2(x,       y),         Vector2(1.0f, h), oH, true);
        r->DrawRectangle(Vector2(x,       y + h - 1.0f), Vector2(w, 1.0f), oS, true);
        r->DrawRectangle(Vector2(x + w - 1.0f, y),   Vector2(1.0f, h), oS, true);
        const MyColor iH(60, 65, 84, 160), iS(14, 16, 26, 160);
        r->DrawRectangle(Vector2(x + 1.0f, y + 1.0f),       Vector2(w - 2.0f, 1.0f), iH, true);
        r->DrawRectangle(Vector2(x + 1.0f, y + 1.0f),       Vector2(1.0f, h - 2.0f), iH, true);
        r->DrawRectangle(Vector2(x + 1.0f, y + h - 2.0f),   Vector2(w - 2.0f, 1.0f), iS, true);
        r->DrawRectangle(Vector2(x + w - 2.0f, y + 1.0f),   Vector2(1.0f, h - 2.0f), iS, true);
    };

    auto DrawSunkenPanel = [&](float x, float y, float w, float h, MyColor bg) {
        r->DrawRectangle(Vector2(x, y), Vector2(w, h), bg, true);
        const MyColor oH(8, 10, 18, 230), oS(88, 94, 118, 220);
        r->DrawRectangle(Vector2(x,       y),         Vector2(w, 1.0f), oH, true);
        r->DrawRectangle(Vector2(x,       y),         Vector2(1.0f, h), oH, true);
        r->DrawRectangle(Vector2(x,       y + h - 1.0f), Vector2(w, 1.0f), oS, true);
        r->DrawRectangle(Vector2(x + w - 1.0f, y),   Vector2(1.0f, h), oS, true);
        const MyColor iH(14, 16, 26, 180), iS(60, 65, 84, 180);
        r->DrawRectangle(Vector2(x + 1.0f, y + 1.0f),       Vector2(w - 2.0f, 1.0f), iH, true);
        r->DrawRectangle(Vector2(x + 1.0f, y + 1.0f),       Vector2(1.0f, h - 2.0f), iH, true);
        r->DrawRectangle(Vector2(x + 1.0f, y + h - 2.0f),   Vector2(w - 2.0f, 1.0f), iS, true);
        r->DrawRectangle(Vector2(x + w - 2.0f, y + 1.0f),   Vector2(1.0f, h - 2.0f), iS, true);
    };

    // --- Window drop shadow ---
    r->DrawRectangle(Vector2(winX + 4.0f, winY + 4.0f), Vector2(winW, winH),
                     MyColor(0, 0, 0, 90), true);

    // --- Window background + outer raised double-bevel ---
    r->DrawRectangle(Vector2(winX, winY), Vector2(winW, winH),
                     MyColor(22, 28, 48, 248), true);
    {
        const MyColor oH(88, 94, 118, 220), oS(8, 10, 18, 220);
        r->DrawRectangle(Vector2(winX,            winY),              Vector2(winW, 1.0f),  oH, true);
        r->DrawRectangle(Vector2(winX,            winY),              Vector2(1.0f, winH),  oH, true);
        r->DrawRectangle(Vector2(winX,            winY + winH - 1.0f), Vector2(winW, 1.0f), oS, true);
        r->DrawRectangle(Vector2(winX + winW - 1.0f, winY),          Vector2(1.0f, winH),  oS, true);
        const MyColor iH(60, 65, 84, 160), iS(14, 16, 26, 160);
        r->DrawRectangle(Vector2(winX + 1.0f, winY + 1.0f),          Vector2(winW - 2.0f, 1.0f),  iH, true);
        r->DrawRectangle(Vector2(winX + 1.0f, winY + 1.0f),          Vector2(1.0f, winH - 2.0f),  iH, true);
        r->DrawRectangle(Vector2(winX + 1.0f, winY + winH - 2.0f),   Vector2(winW - 2.0f, 1.0f),  iS, true);
        r->DrawRectangle(Vector2(winX + winW - 2.0f, winY + 1.0f),   Vector2(1.0f, winH - 2.0f),  iS, true);
    }

    // --- Title bar — solid 3D raised (no images), three-zone shading ---
    // Zone 1: dark base fill (whole bar — establishes deepest colour)
    r->DrawRectangle(Vector2(winX, winY), Vector2(winW, CONSOLE_TITLEBAR_H),
                     MyColor(10, 38, 92, 255), true);
    // Zone 2: bottom shadow band (lower 35% — darkens the bottom for depth)
    r->DrawRectangle(Vector2(winX, winY + std::floor(CONSOLE_TITLEBAR_H * 0.65f)),
                     Vector2(winW, std::ceil(CONSOLE_TITLEBAR_H * 0.35f)),
                     MyColor(4, 14, 42, 200), true);
    // Zone 3: upper lighter band (top 52% — bright highlight pushes top forward)
    r->DrawRectangle(Vector2(winX, winY),
                     Vector2(winW, std::floor(CONSOLE_TITLEBAR_H * 0.52f)),
                     MyColor(44, 96, 182, 215), true);
    // Zone 4: top sheen strip — sharp bright line at very top (glass edge)
    r->DrawRectangle(Vector2(winX + 1.0f, winY + 1.0f), Vector2(winW - 2.0f, 2.0f),
                     MyColor(105, 165, 240, 110), true);
    // Outer raised bevel on title bar
    {
        const MyColor oH(88, 94, 118, 200), oS(6, 8, 18, 230);
        r->DrawRectangle(Vector2(winX,              winY),                              Vector2(winW, 1.0f),              oH, true);
        r->DrawRectangle(Vector2(winX,              winY),                              Vector2(1.0f, CONSOLE_TITLEBAR_H), oH, true);
        r->DrawRectangle(Vector2(winX,              winY + CONSOLE_TITLEBAR_H - 1.0f), Vector2(winW, 1.0f),              oS, true);
        r->DrawRectangle(Vector2(winX + winW - 1.0f, winY),                            Vector2(1.0f, CONSOLE_TITLEBAR_H), oS, true);
        const MyColor iH(60, 65, 84, 140), iS(14, 16, 26, 150);
        r->DrawRectangle(Vector2(winX + 1.0f, winY + 1.0f),                            Vector2(winW - 2.0f, 1.0f),                   iH, true);
        r->DrawRectangle(Vector2(winX + 1.0f, winY + 1.0f),                            Vector2(1.0f, CONSOLE_TITLEBAR_H - 2.0f),     iH, true);
        r->DrawRectangle(Vector2(winX + 1.0f, winY + CONSOLE_TITLEBAR_H - 2.0f),       Vector2(winW - 2.0f, 1.0f),                   iS, true);
        r->DrawRectangle(Vector2(winX + winW - 2.0f, winY + 1.0f),                     Vector2(1.0f, CONSOLE_TITLEBAR_H - 2.0f),     iS, true);
    }

    const float tbTextY = winY + (CONSOLE_TITLEBAR_H - fontSize) * 0.5f - 1.0f;
    r->DrawMyText(L"Console",
                  Vector2(winX + 8.0f, tbTextY),
                  MyColor(220, 235, 255, 255),
                  fontSize);

    // --- Content area background (~70% opacity so scene shows through) ---
    const float contentX = winX;
    const float contentY = winY + CONSOLE_TITLEBAR_H;
    r->DrawRectangle(Vector2(contentX, contentY), Vector2(winW, contentH),
                     MyColor(12, 18, 30, 178), true);

    // --- Scrollbar (sunken track) ---
    const float sbX = contentX + winW - CONSOLE_SCROLLBAR_W;
    // 1px separator between text area and scrollbar
    r->DrawRectangle(Vector2(sbX - 1.0f, contentY), Vector2(1.0f, textAreaH),
                     MyColor(8, 10, 18, 210), true);
    DrawSunkenPanel(sbX, contentY, CONSOLE_SCROLLBAR_W, textAreaH, MyColor(15, 24, 44, 255));

    const float trackX = sbX + 1.0f;
    const float trackY = contentY + 1.0f;
    const float trackW = CONSOLE_SCROLLBAR_W - 2.0f;
    const float trackH = textAreaH - 2.0f;

    m_sbX = trackX;
    m_sbY = trackY;
    m_sbH = trackH;

    // --- Text lines and scrollbar thumb (under buffer lock) ---
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const int totalLines = static_cast<int>(m_buffer.size());
        const int maxOffset  = std::max(0, totalLines - visibleLines);
        m_scrollOffset       = std::clamp(m_scrollOffset, 0, maxOffset);

        // Scrollbar thumb — raised mini-panel for 3D look
        if (totalLines > 0) {
            const float fillRatio = std::min(1.0f,
                static_cast<float>(visibleLines) / static_cast<float>(totalLines));
            const float thumbH = std::max(8.0f, trackH * fillRatio);
            const float thumbT = (maxOffset > 0)
                ? (trackH - thumbH) * (1.0f - static_cast<float>(m_scrollOffset) /
                                                static_cast<float>(maxOffset))
                : trackH - thumbH;

            const float tx = trackX + 1.0f;
            const float ty = trackY + thumbT;
            const float tw = trackW - 2.0f;
            const float th = thumbH;

            // Thumb drop shadow
            r->DrawRectangle(Vector2(tx + 1.0f, ty + 1.0f), Vector2(tw, th),
                             MyColor(0, 0, 0, 65), true);
            // Thumb body
            r->DrawRectangle(Vector2(tx, ty), Vector2(tw, th),
                             MyColor(52, 110, 200, 255), true);
            // Top highlight
            r->DrawRectangle(Vector2(tx, ty), Vector2(tw, 1.0f),
                             MyColor(90, 155, 235, 220), true);
            // Bottom shadow
            r->DrawRectangle(Vector2(tx, ty + th - 1.0f), Vector2(tw, 1.0f),
                             MyColor(18, 52, 108, 220), true);
            // Left highlight
            r->DrawRectangle(Vector2(tx, ty), Vector2(1.0f, th),
                             MyColor(80, 140, 220, 180), true);
            // Right shadow
            r->DrawRectangle(Vector2(tx + tw - 1.0f, ty), Vector2(1.0f, th),
                             MyColor(18, 48, 100, 180), true);

            // Cache knob bounds for mouse hit-testing in HandleScrollbarClick.
            m_knobY = ty;
            m_knobH = th;
        } else {
            m_knobY = trackY;
            m_knobH = 0.0f;
        }

        // Determine which buffer lines are visible.
        int endIdx   = std::clamp(totalLines - m_scrollOffset, 0, totalLines);
        int startIdx = std::max(0, endIdx - visibleLines);

        const float textAreaW = winW - CONSOLE_SCROLLBAR_W - CONSOLE_PADDING;
        const int approxCPL = std::max(1, static_cast<int>(textAreaW / (fontSize * 0.60f)));

        // Word-wrap buffer entries into flat display rows (no renderer text-measurement).
        struct DisplayRow { std::wstring text; MyColor color; };
        std::vector<DisplayRow> displayRows;
        displayRows.reserve(static_cast<size_t>(visibleLines) * 2);

        for (int i = startIdx; i < endIdx; ++i) {
            const ConsoleLine& cl = m_buffer[i];
            MyColor lineColor;
            switch (cl.type) {
            case ConsoleLineColor::Warning: lineColor = MyColor(255, 220,   0, 255); break;
            case ConsoleLineColor::Error:   lineColor = MyColor(255, 140,   0, 255); break;
            default:                        lineColor = MyColor(210, 210, 210, 255); break;
            }

            const std::wstring& src = cl.text;
            if (src.empty() || static_cast<int>(src.size()) <= approxCPL) {
                displayRows.push_back({src, lineColor});
            } else {
                std::wstring curRow;
                int curLen = 0;
                size_t pos = 0;
                while (pos < src.size()) {
                    size_t wEnd = src.find(L' ', pos);
                    const bool lastWord = (wEnd == std::wstring::npos);
                    if (lastWord) wEnd = src.size();
                    const int wLen   = static_cast<int>(wEnd - pos);
                    const int needed = wLen + (curLen > 0 ? 1 : 0);
                    if (curLen + needed > approxCPL && curLen > 0) {
                        displayRows.push_back({curRow, lineColor});
                        curRow.clear();
                        curLen = 0;
                    }
                    if (curLen > 0) { curRow += L' '; ++curLen; }
                    curRow.append(src, pos, static_cast<size_t>(wLen));
                    curLen += wLen;
                    if (lastWord) break;
                    pos = wEnd + 1;
                }
                if (!curRow.empty())
                    displayRows.push_back({curRow, lineColor});
            }
        }

        // Render display rows anchored to the bottom of the text area.
        const int totalDR   = static_cast<int>(displayRows.size());
        const int showStart = std::max(0, totalDR - visibleLines);
        const int showCount = totalDR - showStart;
        const float textStartY = contentY + CONSOLE_PADDING +
                                 static_cast<float>(visibleLines - showCount) * lineHeight;

        for (int i = 0; i < showCount; ++i) {
            const DisplayRow& dr = displayRows[static_cast<size_t>(showStart + i)];
            r->DrawMyText(dr.text,
                          Vector2(contentX + CONSOLE_PADDING, textStartY + static_cast<float>(i) * lineHeight),
                          Vector2(textAreaW, lineHeight),
                          dr.color,
                          fontSize);
        }
    }

    // --- Command-line separator — ridge line (dark groove + light counter-edge) ---
    const float cmdSepY = contentY + textAreaH;
    r->DrawRectangle(Vector2(contentX, cmdSepY),        Vector2(winW, 1.0f),
                     MyColor(6, 8, 18, 240), true);
    r->DrawRectangle(Vector2(contentX, cmdSepY + 1.0f), Vector2(winW, 1.0f),
                     MyColor(55, 68, 100, 200), true);

    // --- Command-line bar (raised panel) ---
    const float cmdBarY = cmdSepY + 2.0f;
    const float cmdBarH = CONSOLE_CMDBAR_H - 1.0f;
    DrawRaisedPanel(contentX, cmdBarY, winW, cmdBarH, MyColor(16, 22, 42, 235));

    const float cmdTextY = cmdBarY + (cmdBarH - fontSize) * 0.5f - 1.0f;
    r->DrawMyText(L"> " + m_cmdLine + (m_cursorVisible ? L"_" : L""),
                  Vector2(contentX + CONSOLE_PADDING, cmdTextY),
                  MyColor(160, 220, 160, 255),
                  fontSize);
}
