// ---------------------------------------------------------------------------------------------------------------
// DX12RenderFrame.h
// Notes Last Modified: 04-06-2026
//
// Companion header for DX12RenderFrame.cpp.  Contains helper types that are private to the DX12 render-frame
// implementation and must be visible to DX12Renderer.h (which includes this file).
//
// DO NOT include this file directly — it is included by DX12Renderer.h under the __USE_DIRECTX_12__ guard.
// ---------------------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"

#if defined(__USE_DIRECTX_12__)

// ------------------------------------
// Delta Time Smoothing for DX12
// ------------------------------------
// Mirrors the identical class in DX11Renderer.h; kept separate so each renderer carries its own copy
// and either renderer can evolve independently without cross-contamination.
class DX12DeltaTimeSmoothing
{
private:
    static constexpr int   HISTORY_SIZE     = 8;                // Number of previous deltas to track
    static constexpr float MAX_DELTA_VARIANCE = 0.004f;         // Maximum allowed variance (4 ms)
    float  deltaHistory[HISTORY_SIZE];                          // Circular buffer for delta history
    int    historyIndex;                                        // Current position in circular buffer
    float  smoothedDelta;                                       // Current smoothed delta value
    float  frameTargetTime;                                     // Target frame time based on refresh rate
    float  accumulatedError;                                    // Accumulated timing error for correction
    bool   isInitialized;                                       // Whether smoothing has been seeded

public:
    DX12DeltaTimeSmoothing()
        : historyIndex(0), smoothedDelta(0.0f), frameTargetTime(1.0f / 60.0f),
          accumulatedError(0.0f), isInitialized(false)
    {
        for (int i = 0; i < HISTORY_SIZE; ++i)
            deltaHistory[i] = frameTargetTime;
    }

    // Process raw delta time and return the smoothed value.
    float ProcessDelta(float rawDelta, float refreshRate)
    {
        frameTargetTime = 1.0f / refreshRate;

        float clampedDelta = std::clamp(rawDelta, frameTargetTime * 0.5f, frameTargetTime * 2.0f);

        deltaHistory[historyIndex] = clampedDelta;
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;

        if (!isInitialized)
        {
            if (historyIndex >= HISTORY_SIZE - 1)
                isInitialized = true;
            smoothedDelta = clampedDelta;
            return smoothedDelta;
        }

        float weightedSum = 0.0f;
        float totalWeight = 0.0f;
        for (int i = 0; i < HISTORY_SIZE; ++i)
        {
            float weight = static_cast<float>(i + 1) / static_cast<float>(HISTORY_SIZE);
            int   index  = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
            weightedSum += deltaHistory[index] * weight;
            totalWeight += weight;
        }

        float averageDelta    = weightedSum / totalWeight;
        float targetDiff      = frameTargetTime - averageDelta;
        accumulatedError     += targetDiff * 0.1f;

        float correctedDelta  = averageDelta + std::clamp(accumulatedError, -MAX_DELTA_VARIANCE, MAX_DELTA_VARIANCE);
        smoothedDelta         = correctedDelta;

        return smoothedDelta;
    }

    float GetSmoothedDelta() const { return smoothedDelta; }

    void Reset()
    {
        historyIndex     = 0;
        accumulatedError = 0.0f;
        isInitialized    = false;
        for (int i = 0; i < HISTORY_SIZE; ++i)
            deltaHistory[i] = frameTargetTime;
    }
};

#endif // defined(__USE_DIRECTX_12__)
