#pragma once

#include <algorithm>
#include <cmath>

// Constant-cost, allocation-free feedback from measured frame time. This learns the
// actual scene/system budget rather than guessing detail from a CPU/GPU model name.
// Frame pacing is separate: fixed qualities use the same FPS cap without adapting.
class AdaptiveQuality
{
public:
    float Update(float frameSeconds, int requestedFps, bool automatic, bool reset = false)
    {
        if (!automatic) { m_active = false; return m_detail; }
        const int fps = (std::clamp)(requestedFps, 20, 240);
        if (reset || !m_active || fps != m_fps)
        {
            // Refit after a scene/target/mode change; a heavy previous scene must not
            // leave a lighter scene at low detail behind a long failed-probe cooldown.
            m_detail = 1.0f;
            m_ema = m_bad = m_good = 0.0f;
            m_wait = 6.0f;
            m_sinceRaise = 1000.0f;
            m_probe = false;
            m_active = true;
            m_fps = fps;
        }
        if (!std::isfinite(frameSeconds) || frameSeconds <= 0.0f || frameSeconds >= 0.25f)
        {
            // Scene loading, window drags and alt-tab are not sustainable render cost.
            m_ema = m_bad = m_good = 0.0f;
            return m_detail;
        }

        const float budget = 1.0f / static_cast<float>(fps);
        // A time-based smoothing window behaves consistently at 30, 60, 90 and 120 FPS.
        const float weight = (std::min)(1.0f, frameSeconds / 0.25f);
        m_ema = m_ema <= 0.0f ? frameSeconds : m_ema + (frameSeconds - m_ema) * weight;
        m_sinceRaise += frameSeconds;
        if (m_ema > budget * 1.04f)
        {
            m_good = 0.0f;
            m_bad += frameSeconds;
            if (m_bad >= 0.3f)
            {
                m_detail = (std::max)(0.1f, m_detail - 0.1f);
                m_bad = 0.0f;
                // Penalise one failed upward probe once, rather than doubling the
                // cooldown again on each downward step during the same overload.
                if (m_probe && m_sinceRaise < 5.0f) m_wait = (std::min)(60.0f, m_wait * 2.0f);
                m_probe = false;
            }
        }
        else
        {
            m_bad = 0.0f;
            m_good += frameSeconds;
            if (m_good >= m_wait && m_detail < 1.0f)
            {
                m_detail = (std::min)(1.0f, m_detail + 0.05f);
                m_good = m_sinceRaise = 0.0f;
                m_wait = (std::max)(6.0f, m_wait * 0.8f);
                m_probe = true;
            }
        }
        return m_detail;
    }

private:
    float m_detail = 1.0f, m_ema = 0.0f, m_bad = 0.0f, m_good = 0.0f;
    float m_wait = 6.0f, m_sinceRaise = 1000.0f;
    int m_fps = 0;
    bool m_active = false, m_probe = false;
};
