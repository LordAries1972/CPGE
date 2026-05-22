#pragma once

#include <atomic>
#include <functional>

// Forward declaration
class Renderer;

// --- Position & size (edit these to reposition / resize the console) --------
constexpr float CONSOLE_WIN_X         = 10.0f;    // left edge (px from client left)
constexpr float CONSOLE_WIN_Y         =  5.0f;    // top edge  (px from client top)
constexpr float CONSOLE_WIN_H_MARGIN  = 10.0f;    // px subtracted from client height → window height
constexpr float CONSOLE_WIN_W_PCT     =  0.75f;   // window width as fraction of client width
constexpr float CONSOLE_WIN_W_MIN     = 570.0f;   // minimum window width  (px)
constexpr float CONSOLE_WIN_W_MAX     = 1400.0f;  // maximum window width  (px)
// ----------------------------------------------------------------------------

constexpr int   CONSOLE_MAX_LINES     = 500;
constexpr int   CONSOLE_VISIBLE_LINES = 14;  // default; overridden each frame by Render()
constexpr float CONSOLE_TITLEBAR_H    = 20.0f;
constexpr float CONSOLE_CMDBAR_H      = 22.0f;
constexpr float CONSOLE_SCROLLBAR_W   = 10.0f;
constexpr float CONSOLE_PADDING       = 3.0f;

enum class ConsoleLineColor { Normal, Warning, Error };

struct ConsoleLine {
    std::wstring     text;
    ConsoleLineColor type = ConsoleLineColor::Normal;
};

// -----------------------------------------------------------------------
// ConsoleWindow
// -----------------------------------------------------------------------
// An F8-toggled OSD console that displays up to m_visibleLines rows
// (computed each frame from client height - 5) of text from a CONSOLE_MAX_LINES
// circular buffer, with a scrollbar on
// the right edge showing buffer position.  Newest line is always at the
// bottom.  Thread-safe for AddLine() called from any thread.
//
// Rendered only in SCENE_GAMETITLE / SCENE_GAMEPLAY; the render-site in
// DXRenderFrame.cpp skips the draw when bSceneSwitching is true.
// -----------------------------------------------------------------------
class ConsoleWindow {
public:
    ConsoleWindow() = default;

    void Toggle();
    void AddLine(const std::wstring& line, ConsoleLineColor type = ConsoleLineColor::Normal);
    void Scroll(int delta);
    void HandleMouseWheel(int delta);
    void HandleMouseClick(float x, float y);
    void HandleChar(wchar_t c);
    void HandleBackspace();
    void HandleEnter();

    // Register a callback invoked when the user presses Enter.
    // Receives the raw command text (no "> " prefix) as a wide string.
    void SetCommandCallback(std::function<void(const std::wstring&)> cb);

    void Render(Renderer* r, int screenWidth, int screenHeight);
    void Clear();

    bool bIsVisible = false;

private:
    std::deque<ConsoleLine>  m_buffer;
    std::mutex               m_mutex;
    int                      m_scrollOffset = 0;  // lines scrolled up from bottom (0 = newest visible)

    // Scrollbar track rect cached each Render() frame for mouse hit-testing
    float m_sbX = 0.0f;
    float m_sbY = 0.0f;
    float m_sbH = 0.0f;

    // Visible-line count computed from client height each frame; atomic for cross-thread Scroll()
    std::atomic<int> m_visibleLines { CONSOLE_VISIBLE_LINES };

    // Command line input buffer (main-thread only)
    std::wstring m_cmdLine;

    // Invoked by HandleEnter() with the submitted command string
    std::function<void(const std::wstring&)> m_commandCallback;
};
