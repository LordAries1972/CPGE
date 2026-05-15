#pragma once

#define NOMINMAN

#include "Includes.h"
#include "ConsoleWindow.h"
#include "Renderer.h"
#include "Color.h"
#include "Vectors.h"
#include "Debug.h"
#include "ThreadManager.h"

extern Debug debug;
extern ThreadManager threadManager;

// Global instance — referenced as extern ConsoleWindow consoleWindow in consuming files.
ConsoleWindow consoleWindow;

void ConsoleWindow::Toggle()
{
    bIsVisible = !bIsVisible;
}

void ConsoleWindow::AddLine(const std::wstring& line, ConsoleLineColor type)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.push_back({line, type});
    while (static_cast<int>(m_buffer.size()) > CONSOLE_MAX_LINES)
        m_buffer.pop_front();
    m_scrollOffset = 0; // auto-scroll to newest on every new line
}

void ConsoleWindow::Scroll(int delta)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const int totalLines = static_cast<int>(m_buffer.size());
    const int maxOffset  = std::max(0, totalLines - CONSOLE_VISIBLE_LINES);
    m_scrollOffset = std::clamp(m_scrollOffset + delta, 0, maxOffset);
}

void ConsoleWindow::HandleMouseWheel(int delta)
{
    if (!bIsVisible) return;
    Scroll(delta > 0 ? 3 : -3);
}

void ConsoleWindow::HandleMouseClick(float x, float y)
{
    if (!bIsVisible || m_sbH <= 0.0f) return;
    if (x < m_sbX || x > m_sbX + CONSOLE_SCROLLBAR_W) return;
    if (y < m_sbY || y > m_sbY + m_sbH) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    const int totalLines = static_cast<int>(m_buffer.size());
    const int maxOffset  = std::max(0, totalLines - CONSOLE_VISIBLE_LINES);
    if (maxOffset == 0) return;

    // top of track = maxOffset (oldest); bottom = 0 (newest)
    const float t  = 1.0f - (y - m_sbY) / m_sbH;
    m_scrollOffset = std::clamp(static_cast<int>(t * static_cast<float>(maxOffset) + 0.5f), 0, maxOffset);
}

void ConsoleWindow::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_buffer.clear();
    m_scrollOffset = 0;
}

void ConsoleWindow::Render(Renderer* r, int screenWidth, int screenHeight)
{
    if (!bIsVisible || !r) return;
    if (!r->bIsInitialized.load()) return;
    if (threadManager.threadVars.bIsShuttingDown.load() ||
        threadManager.threadVars.bIsResizing.load()) return;

    // Font size scaled to screen height; ~10pt at 1080p, clamped to [8, 12].
    const float fontSize   = std::clamp(static_cast<float>(screenHeight) / 108.0f, 8.0f, 12.0f);
    const float lineHeight = fontSize + 4.0f;

    // Window geometry — bottom-left corner, 80% of screen width.
    const float winW     = std::clamp(static_cast<float>(screenWidth) * 0.80f, 600.0f, 1400.0f);
    const float contentH = lineHeight * static_cast<float>(CONSOLE_VISIBLE_LINES) + CONSOLE_PADDING * 2.0f;
    const float winH     = CONSOLE_TITLEBAR_H + contentH;
    const float winX     = 10.0f;
    const float winY     = static_cast<float>(screenHeight) - winH - 50.0f;

    // --- Outer border — bright blue, clearly visible on any dark background ---
    r->DrawRectangle(Vector2(winX - 1.0f, winY - 1.0f),
                     Vector2(winW + 2.0f, winH + 2.0f),
                     MyColor(60, 140, 220, 230), true);

    // --- Titlebar ---
    r->DrawRectangle(Vector2(winX, winY),
                     Vector2(winW, CONSOLE_TITLEBAR_H),
                     MyColor(20, 55, 100, 245), true);

    // "Console" label, vertically centred in titlebar
    const float tbTextY = winY + (CONSOLE_TITLEBAR_H - fontSize) * 0.5f - 1.0f;
    r->DrawMyText(L"Console",
                  Vector2(winX + 6.0f, tbTextY),
                  MyColor(220, 235, 255, 255),
                  fontSize);

    // Titlebar bottom separator
    r->DrawRectangle(Vector2(winX, winY + CONSOLE_TITLEBAR_H - 1.0f),
                     Vector2(winW, 1.0f),
                     MyColor(60, 120, 190, 255), true);

    // --- Content area — dark semi-transparent background ---
    const float contentX = winX;
    const float contentY = winY + CONSOLE_TITLEBAR_H;
    r->DrawRectangle(Vector2(contentX, contentY),
                     Vector2(winW, contentH),
                     MyColor(12, 18, 30, 218), true);

    // --- Scrollbar track (right edge of content area) ---
    const float sbX = contentX + winW - CONSOLE_SCROLLBAR_W;
    m_sbX = sbX;
    m_sbY = contentY;
    m_sbH = contentH;
    r->DrawRectangle(Vector2(sbX, contentY),
                     Vector2(CONSOLE_SCROLLBAR_W, contentH),
                     MyColor(18, 35, 60, 255), true);

    // Separator between text area and scrollbar
    r->DrawRectangle(Vector2(sbX, contentY),
                     Vector2(1.0f, contentH),
                     MyColor(55, 110, 175, 255), true);

    // --- Text lines and scrollbar thumb (under lock) ---
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const int totalLines = static_cast<int>(m_buffer.size());
        const int maxOffset  = std::max(0, totalLines - CONSOLE_VISIBLE_LINES);
        m_scrollOffset       = std::clamp(m_scrollOffset, 0, maxOffset);

        // Scrollbar thumb — proportional to fraction of buffer that is visible.
        if (totalLines > 0)
        {
            const float fillRatio = std::min(1.0f,
                static_cast<float>(CONSOLE_VISIBLE_LINES) / static_cast<float>(totalLines));
            const float thumbH = std::max(8.0f, contentH * fillRatio);

            // scrollOffset=0 → thumb at bottom; scrollOffset=maxOffset → thumb at top.
            const float thumbT = (maxOffset > 0)
                ? (contentH - thumbH) * (1.0f - static_cast<float>(m_scrollOffset) /
                                                  static_cast<float>(maxOffset))
                : contentH - thumbH;

            r->DrawRectangle(Vector2(sbX + 1.0f, contentY + thumbT),
                             Vector2(CONSOLE_SCROLLBAR_W - 2.0f, thumbH),
                             MyColor(80, 150, 225, 255), true);
        }

        // Which lines to show (newest at bottom).
        int endIdx   = std::clamp(totalLines - m_scrollOffset, 0, totalLines);
        int startIdx = std::max(0, endIdx - CONSOLE_VISIBLE_LINES);
        int lineCount = endIdx - startIdx;

        // Push lines to the bottom of the content area when buffer is sparse.
        const float textStartY = contentY + CONSOLE_PADDING +
                                 static_cast<float>(CONSOLE_VISIBLE_LINES - lineCount) * lineHeight;

        for (int i = 0; i < lineCount; ++i)
        {
            const ConsoleLine& cl = m_buffer[startIdx + i];
            MyColor lineColor;
            switch (cl.type)
            {
            case ConsoleLineColor::Warning: lineColor = MyColor(255, 220,   0, 255); break;
            case ConsoleLineColor::Error:   lineColor = MyColor(255, 140,   0, 255); break;
            default:                        lineColor = MyColor(210, 210, 210, 255); break;
            }
            r->DrawMyText(cl.text,
                          Vector2(contentX + CONSOLE_PADDING, textStartY + static_cast<float>(i) * lineHeight),
                          lineColor,
                          fontSize);
        }
    }
}
