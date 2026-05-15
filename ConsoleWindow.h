#pragma once

// Forward declaration
class Renderer;

constexpr int   CONSOLE_MAX_LINES     = 100;
constexpr int   CONSOLE_VISIBLE_LINES = 14;
constexpr float CONSOLE_TITLEBAR_H    = 20.0f;
constexpr float CONSOLE_SCROLLBAR_W   = 10.0f;
constexpr float CONSOLE_PADDING       = 4.0f;

enum class ConsoleLineColor { Normal, Warning, Error };

struct ConsoleLine {
    std::wstring     text;
    ConsoleLineColor type = ConsoleLineColor::Normal;
};

// -----------------------------------------------------------------------
// ConsoleWindow
// -----------------------------------------------------------------------
// An F8-toggled OSD console that displays up to CONSOLE_VISIBLE_LINES rows
// of text from a CONSOLE_MAX_LINES circular buffer, with a scrollbar on
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
    void Render(Renderer* r, int screenWidth, int screenHeight);
    void Clear();

    bool bIsVisible = false;

private:
    std::deque<ConsoleLine>  m_buffer;
    std::mutex               m_mutex;
    int                      m_scrollOffset = 0;  // lines scrolled up from bottom (0 = newest visible)

    // Scrollbar rect cached each Render() frame for mouse hit-testing
    float m_sbX = 0.0f;
    float m_sbY = 0.0f;
    float m_sbH = 0.0f;
};
