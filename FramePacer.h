#pragma once

#include "Includes.h"

#include <algorithm>
#include <chrono>
#include <thread>

// One instance per renderer, used only under its existing exclusive render lock.
// The selected FPS applies to every quality level; Auto adjusts detail separately.
// Wait before sampling input so the frame uses fresh input after any pacing delay.
class FramePacer
{
public:
    FramePacer() = default;
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    ~FramePacer()
    {
        #if defined(PLATFORM_WINDOWS)
            if (m_timer) CloseHandle(m_timer);
        #endif
    }

    void Wait(int requestedFps)
    {
        const int fps = (std::clamp)(requestedFps, 20, 240);
        const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / fps));
        auto now = Clock::now();
        if (fps != m_fps)
        {
            // A new setting takes effect immediately. Do not inherit an old deadline.
            m_fps = fps;
            m_next = now;
        }

        // Work that already missed the next start consumes the entire budget.
        // Rebase now so even a small overrun cannot cause a short catch-up frame.
        if (now > m_next) m_next = now;
        if (now < m_next)
        {
            #if defined(PLATFORM_WINDOWS)
                if (!m_timerTried)
                {
                    m_timerTried = true;
                    // Windows 10 1803+ supports this flag; fall back on older hosts.
                    constexpr DWORD highResolutionTimer = 0x00000002;
                    m_timer = CreateWaitableTimerExW(nullptr, nullptr, highResolutionTimer, TIMER_MODIFY_STATE | SYNCHRONIZE);
                    if (!m_timer) m_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
                }
                bool waited = false;
                if (m_timer)
                {
                    LARGE_INTEGER due;
                    // Negative values are relative 100 ns intervals. No timer-resolution
                    // changes or CPU spinning: the render thread sleeps for its spare budget.
                    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(m_next - now).count() / 100;
                    due.QuadPart = -(std::max)(1LL, static_cast<long long>(ticks));
                    if (SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE))
                        waited = WaitForSingleObject(m_timer, INFINITE) == WAIT_OBJECT_0;
                }
                if (!waited) std::this_thread::sleep_until(m_next);
            #else
                std::this_thread::sleep_until(m_next);
            #endif
            now = Clock::now();
        }

        // Keep absolute deadlines through small scheduler jitter. After an overloaded
        // frame or a pause, rebase instead of issuing a burst of catch-up frames.
        if (now - m_next >= period) m_next = now;
        m_next += period;
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_next{};
    int m_fps = 0;
    #if defined(PLATFORM_WINDOWS)
        HANDLE m_timer = nullptr;
        bool m_timerTried = false;
    #endif
};
