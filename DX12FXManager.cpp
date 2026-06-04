#include "Includes.h"

#if defined(__USE_DIRECTX_12__)
#if defined(_WIN32) || defined(_WIN64)

#include "DX12FXManager.h"
#include "DX12Renderer.h"
#include "Debug.h"
#include "MathPrecalculation.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"

extern Debug debug;
extern ThreadManager threadManager;

#pragma warning(push)
#pragma warning(disable: 4101)
#pragma warning(disable: 4267)

// ============================================================
// Constructor / Destructor
// ============================================================

FXManager::FXManager() : bHasCleanedUp(false), bIsRendering(false) {}

FXManager::~FXManager() {
    CleanUp();
}

// ============================================================
// CleanUp
// ============================================================

void FXManager::CleanUp()
{
    if (bHasCleanedUp) return;
    bIsRendering.store(false);
    effects.clear();
    pendingCallbacks.clear();
    bHasCleanedUp = true;
}

// ============================================================
// Initialize — DX12 path: uses D2D via renderer; no raw DX12 resources needed here.
// ============================================================

void FXManager::Initialize() {
    if (bHasCleanedUp) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12FXManager] Cannot initialize - already cleaned up");
        return;
    }
    if (!renderer || !renderer->bIsInitialized.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX12FXManager] Renderer not ready — deferring initialization");
        return;
    }
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12FXManager] Initialized (D2D-backed)");
}

// LoadFadeShaders — not required for DX12 (color fades use D2D DrawRectangle)
bool FXManager::LoadFadeShaders() { return true; }

// SaveRenderState / RestoreRenderState — D2D manages render state internally
void FXManager::SaveRenderState()    {}
void FXManager::RestoreRenderState() {}

// ============================================================
// IsFadeActive
// ============================================================

bool FXManager::IsFadeActive() const {
    for (const auto& effect : effects) {
        if (effect.type == FXType::ColorFader && effect.progress < 1.0f)
            return true;
    }
    return false;
}

// ============================================================
// AddEffect
// ============================================================

void FXManager::AddEffect(const FXItem& fxItem) {
    FXItem newEffect = fxItem;
    newEffect.startTime = std::chrono::steady_clock::now();
    newEffect.lastUpdate = newEffect.startTime;
    effects.push_back(newEffect);
}

// ============================================================
// StopAllFXForResize / RestartFXAfterResize
// ============================================================

void FXManager::StopAllFXForResize()
{
    ThreadLockHelper lock(threadManager, "fxmanager_stop_all_resize_lock", 5000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12FXManager] Failed to acquire lock for StopAllFXForResize");
        return;
    }
    try {
        savedFXState = ActiveFXState{};
        savedFXState.textScrollerIDs.reserve(20);
        savedFXState.activeScrollTextures.reserve(10);

        if (starfieldID > 0) {
            savedFXState.starfieldActive = true;
            savedFXState.starfieldID = starfieldID;
            StopStarfield();
        }
        if (tunnelID > 0) {
            savedFXState.tunnelActive = true;
            savedFXState.tunnelID = tunnelID;
            StopWarpDotTunnel();
        }

        std::vector<int> activeTextScrollerIDs;
        for (const auto& fx : effects) {
            if (fx.type == FXType::TextScroller)
                activeTextScrollerIDs.push_back(fx.fxID);
        }
        for (int id : activeTextScrollerIDs) {
            StopTextScroller(id);
            savedFXState.textScrollerIDs.push_back(id);
        }
        if (!savedFXState.textScrollerIDs.empty())
            savedFXState.textScrollerActive = true;

        bool fadeActive = false;
        for (const auto& fx : effects) {
            if (fx.type == FXType::ColorFader && fx.progress < 1.0f) { fadeActive = true; break; }
        }
        if (fadeActive) savedFXState.fadeEffectActive = true;

        std::vector<FXItem> tempEffects;
        tempEffects.swap(effects);
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12FXManager] Exception in StopAllFXForResize: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
}

void FXManager::RestartFXAfterResize()
{
    try {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        SecureZeroMemory(&savedFXState, sizeof(ActiveFXState));
    }
    catch (...) {}
}

// ============================================================
// ApplyColorFader — DX12 uses renderer->DrawRectangle() via D2D
// ============================================================

void FXManager::ApplyColorFader(FXItem& fxItem) {
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) return;
    if (fxItem.duration <= 0.0f) { fxItem.progress = 1.0f; return; }

    auto now = std::chrono::steady_clock::now();
    if (fxItem.lastUpdate.time_since_epoch().count() == 0)
        fxItem.lastUpdate = fxItem.startTime;

    float totalElapsed = std::chrono::duration<float>(now - fxItem.startTime).count();

    bool shouldUpdate = (std::chrono::duration<float>(now - fxItem.lastUpdate).count() >= fxItem.delay)
                     || (totalElapsed >= fxItem.duration);

    if (shouldUpdate) {
        fxItem.lastUpdate = now;
        fxItem.progress = (totalElapsed >= fxItem.duration)
            ? 1.0f
            : totalElapsed / fxItem.duration;
        fxItem.progress = std::max(0.0f, std::min(1.0f, fxItem.progress));
    }

    float effectiveProgress = fxItem.progress;
    if (fxItem.subtype == FXSubType::FadeToBackground)
        effectiveProgress = 1.0f - fxItem.progress;

    XMFLOAT4 fadeColor = fxItem.targetColor;
    fadeColor.w = std::max(0.0f, std::min(1.0f, effectiveProgress));

    if (!renderer) return;

    // DX12: draw a full-screen rectangle via D2D (DrawRectangle is backed by ID2D1DeviceContext)
    RenderFullScreenQuad(fadeColor);
}

// ============================================================
// RenderFullScreenQuad — DX12 path uses renderer->DrawRectangle()
// ============================================================

void FXManager::RenderFullScreenQuad(const XMFLOAT4& color) {
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) return;
    if (!renderer) return;

    XMFLOAT4 c = {
        std::max(0.0f, std::min(1.0f, color.x)),
        std::max(0.0f, std::min(1.0f, color.y)),
        std::max(0.0f, std::min(1.0f, color.z)),
        std::max(0.0f, std::min(1.0f, color.w))
    };

    // Draw a full-screen rectangle with the fade colour via the D2D-backed DrawRectangle.
    // iOrigWidth/iOrigHeight are in physical pixels which match the D2D coordinate space (96 DPI).
    renderer->DrawRectangle(
        Vector2(0.0f, 0.0f),
        Vector2(static_cast<float>(renderer->iOrigWidth), static_cast<float>(renderer->iOrigHeight)),
        MyColor(
            static_cast<uint8_t>(c.x * 255.0f),
            static_cast<uint8_t>(c.y * 255.0f),
            static_cast<uint8_t>(c.z * 255.0f),
            static_cast<uint8_t>(c.w * 255.0f)),
        true);
}

// ============================================================
// RemoveCompletedEffects
// ============================================================

void FXManager::RemoveCompletedEffects() {
    ThreadLockHelper lock(threadManager, "fxmanager_remove_effects_lock", 1000);
    if (!lock.IsLocked()) return;
    if (effects.empty()) return;

    auto now = std::chrono::steady_clock::now();

    std::vector<size_t> indicesToRemove;
    indicesToRemove.reserve(effects.size());

    for (size_t i = 0; i < effects.size(); ++i) {
        const FXItem& fx = effects[i];
        bool timedOut = std::chrono::duration<float>(now - fx.startTime).count() >= fx.timeout;
        bool progressCompleted = fx.progress >= 1.0f;

        if (fx.type == FXType::TextFadeInOut) {
            if (progressCompleted) indicesToRemove.push_back(i);
            continue;
        }
        if (fx.type == FXType::TextScroller && fx.subtype == FXSubType::TXT_SCROLL_CONSISTANT) {
            if (fx.duration != FLT_MAX && timedOut) indicesToRemove.push_back(i);
        }
        else if (timedOut || progressCompleted) {
            indicesToRemove.push_back(i);
        }
    }

    for (auto it = indicesToRemove.rbegin(); it != indicesToRemove.rend(); ++it) {
        if (*it < effects.size())
            effects.erase(effects.begin() + *it);
    }
}

// ============================================================
// Fade helpers
// ============================================================

void FXManager::FadeToColor(XMFLOAT4 color, float duration, float delay) {
    FXItem fadeEffect;
    fadeEffect.type = FXType::ColorFader;
    fadeEffect.subtype = FXSubType::FadeToTargetColor;
    fadeEffect.duration = duration; fadeEffect.delay = delay;
    fadeEffect.timeout = duration + 1.0f; fadeEffect.progress = 0.0f;
    fadeEffect.targetColor = color;
    AddEffect(fadeEffect);
}
void FXManager::FadeToBlack(float duration, float delay) { FadeToColor(XMFLOAT4(0,0,0,1), duration, delay); }
void FXManager::FadeToWhite(float duration, float delay) { FadeToColor(XMFLOAT4(1,1,1,1), duration, delay); }

void FXManager::FadeToImage(float duration, float delay) {
    FXItem fadeEffect;
    fadeEffect.type = FXType::ColorFader; fadeEffect.subtype = FXSubType::FadeToBackground;
    fadeEffect.duration = duration; fadeEffect.delay = delay;
    fadeEffect.timeout = duration + 1.0f; fadeEffect.progress = 0.0f;
    fadeEffect.targetColor = XMFLOAT4(0,0,0,1);
    AddEffect(fadeEffect);
}

void FXManager::FadeOutThenCallback(XMFLOAT4 color, float duration, float delay, std::function<void()> callback) {
    if (!callback) return;
    if (duration <= 0.0f || delay < 0.0f) return;

    ThreadLockHelper lock(threadManager, "fxmanager_callback_lock", 1000);
    if (!lock.IsLocked()) return;

    try {
        FXItem fadeEffect;
        fadeEffect.type = FXType::ColorFader; fadeEffect.subtype = FXSubType::FadeToTargetColor;
        fadeEffect.fxID = static_cast<int>(effects.size()) + 1000;
        fadeEffect.duration = duration; fadeEffect.delay = delay;
        fadeEffect.timeout = duration + delay + 2.0f; fadeEffect.progress = 0.0f;
        fadeEffect.targetColor = color;
        fadeEffect.startTime = std::chrono::steady_clock::now();
        fadeEffect.lastUpdate = fadeEffect.startTime;

        bool idExists = false;
        for (const auto& existingFx : effects) {
            if (existingFx.fxID == fadeEffect.fxID) { idExists = true; break; }
        }
        if (idExists)
            fadeEffect.fxID = static_cast<int>(effects.size()) + static_cast<int>(pendingCallbacks.size()) + 2000;

        AddEffect(fadeEffect);
        pendingCallbacks.push_back(CallbackEntry(fadeEffect.fxID, callback));
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12FXManager] FadeOutThenCallback exception: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
}

void FXManager::FadeOutInSequence(XMFLOAT4 fadeOutColor, XMFLOAT4 fadeInColor, float duration, float delay,
    std::function<void()> midpointCallback) {
    FadeOutThenCallback(fadeOutColor, duration, delay, [=]() {
        if (midpointCallback) midpointCallback();
        FadeToColor(fadeInColor, duration, delay);
    });
}

// ============================================================
// UpdateTweens / Scroller helpers
// ============================================================

void FXManager::UpdateTweens(float deltaTime) {
    for (auto& tween : activeTweens) {
        if (!tween.active) continue;
        tween.elapsed += deltaTime;
        float t = std::min(tween.elapsed / tween.duration, 1.0f);
        int newSpeed = static_cast<int>(tween.from + (tween.to - tween.from) * t);
        UpdateScrollSpeed(tween.textureIndex, newSpeed);
        if (t >= 1.0f) tween.active = false;
    }
    activeTweens.erase(std::remove_if(activeTweens.begin(), activeTweens.end(),
        [](const ScrollTween& t) { return !t.active; }), activeTweens.end());
}

void FXManager::StartParallaxLayer(BlitObj2DIndexType textureIndex, FXSubType direction, int baseSpeed,
    float depthMultiplier, int tileWidth, int tileHeight, float delay, bool cameraLinked)
{
    FXItem fx;
    fx.type = FXType::Scroller; fx.subtype = direction;
    fx.scrollSpeed = baseSpeed; fx.textureIndex = textureIndex;
    fx.tileWidth = tileWidth; fx.tileHeight = tileHeight;
    fx.delay = delay; fx.progress = 0.0f; fx.timeout = FLT_MAX;
    fx.depthMultiplier = depthMultiplier; fx.cameraLinked = cameraLinked;
    fx.startTime = std::chrono::steady_clock::now(); fx.lastUpdate = fx.startTime;
    AddEffect(fx);
}

void FXManager::SetScrollDirection(BlitObj2DIndexType textureIndex, FXSubType newDirection) {
    for (auto& fx : effects)
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex)
            fx.subtype = newDirection;
}

void FXManager::FadeScrollSpeed(BlitObj2DIndexType textureIndex, int fromSpeed, int toSpeed, float duration) {
    UpdateScrollSpeed(textureIndex, fromSpeed);
    activeTweens.push_back({ textureIndex, fromSpeed, toSpeed, duration });
}

void FXManager::PauseScroll(BlitObj2DIndexType textureIndex) {
    for (auto& fx : effects)
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex)
            fx.isPaused = true;
}

void FXManager::ResumeScroll(BlitObj2DIndexType textureIndex) {
    for (auto& fx : effects)
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) {
            fx.isPaused = false;
            fx.lastUpdate = std::chrono::steady_clock::now();
        }
}

void FXManager::UpdateScrollSpeed(BlitObj2DIndexType textureIndex, int newSpeed) {
    for (auto& fx : effects)
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex)
            fx.scrollSpeed = newSpeed;
}

void FXManager::ApplyScroller(FXItem& fxItem) {
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - fxItem.lastUpdate).count();

    renderer->Blit2DWrappedObjectAtOffset(
        fxItem.textureIndex, 0, 0,
        fxItem.currentXOffset, fxItem.currentYOffset,
        fxItem.tileWidth, fxItem.tileHeight);

    if (fxItem.isPaused) return;

    if (elapsed >= fxItem.delay) {
        fxItem.lastUpdate = now;
        int effectiveSpeed = static_cast<int>(fxItem.scrollSpeed * fxItem.depthMultiplier);
        switch (fxItem.subtype) {
        case FXSubType::ScrollRight:       fxItem.currentXOffset += effectiveSpeed; break;
        case FXSubType::ScrollLeft:        fxItem.currentXOffset -= effectiveSpeed; break;
        case FXSubType::ScrollUp:          fxItem.currentYOffset -= effectiveSpeed; break;
        case FXSubType::ScrollDown:        fxItem.currentYOffset += effectiveSpeed; break;
        case FXSubType::ScrollUpAndLeft:   fxItem.currentXOffset -= effectiveSpeed; fxItem.currentYOffset -= effectiveSpeed; break;
        case FXSubType::ScrollUpAndRight:  fxItem.currentXOffset += effectiveSpeed; fxItem.currentYOffset -= effectiveSpeed; break;
        case FXSubType::ScrollDownAndLeft: fxItem.currentXOffset -= effectiveSpeed; fxItem.currentYOffset += effectiveSpeed; break;
        case FXSubType::ScrollDownAndRight:fxItem.currentXOffset += effectiveSpeed; fxItem.currentYOffset += effectiveSpeed; break;
        default: break;
        }
        fxItem.currentXOffset = ((fxItem.currentXOffset % fxItem.tileWidth)  + fxItem.tileWidth)  % fxItem.tileWidth;
        fxItem.currentYOffset = ((fxItem.currentYOffset % fxItem.tileHeight) + fxItem.tileHeight) % fxItem.tileHeight;
    }
}

void FXManager::StopScrollEffect(BlitObj2DIndexType textureIndex) {
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [=](const FXItem& fx) {
            return fx.type == FXType::Scroller && fx.textureIndex == textureIndex;
        }), effects.end());
}

void FXManager::StartScrollEffect(BlitObj2DIndexType textureIndex, FXSubType direction, int speed,
    int tileWidth, int tileHeight, float delay)
{
    FXItem fx;
    fx.type = FXType::Scroller; fx.subtype = direction;
    fx.scrollSpeed = speed; fx.textureIndex = textureIndex;
    fx.tileWidth = tileWidth; fx.tileHeight = tileHeight;
    fx.delay = delay; fx.progress = 0.0f; fx.timeout = FLT_MAX;
    fx.startTime = std::chrono::steady_clock::now(); fx.lastUpdate = fx.startTime;
    AddEffect(fx);
}

// ============================================================
// CancelEffect / RestartEffect / ChainEffect
// ============================================================

void FXManager::CancelEffect(int effectID) {
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [effectID](const FXItem& fx) {
            return fx.fxID == effectID;
        }), effects.end());
}

void FXManager::RestartEffect(int effectID) {
    for (auto& fx : effects) {
        if (fx.fxID == effectID) {
            fx.startTime = std::chrono::steady_clock::now();
            fx.lastUpdate = fx.startTime;
            fx.progress = 0.0f;
        }
    }
}

void FXManager::ChainEffect(int fromEffectID, int toEffectID) {
    for (auto& fx : effects) {
        if (fx.fxID == fromEffectID)
            fx.nextEffectID = toEffectID;
    }
}

// ============================================================
// Particle Explosion
// ============================================================

void FXManager::CreateParticleExplosion(int startX, int startY, int maxParticles, int maxRadius) {
    std::lock_guard<std::mutex> lock(m_effectsMutex);

    FXItem newFX;
    newFX.type = FXType::ParticleExplosion;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    newFX.originX = startX; newFX.originY = startY;
    newFX.duration = 3.0f; newFX.timeout = 5.0f;

    const float PI = 3.14159265f;
    float angleStep = 2.0f * PI / static_cast<float>(maxParticles);
    const float colors[15][3] = {
        {1,0,0},{1,.5f,0},{1,1,0},{0,1,0},{0,1,1},
        {0,0,1},{.5f,0,1},{1,0,1},{1,0,.5f},{.7f,.7f,.7f},
        {1,.8f,.2f},{.3f,1,.3f},{.9f,.2f,.9f},{.6f,.6f,1},{.8f,.4f,.2f}
    };

    for (int i = 0; i < maxParticles; ++i) {
        Particle p;
        p.angle = angleStep * i + (static_cast<float>(rand()) / RAND_MAX * 0.2f - 0.1f);
        p.delayCount = rand() % 3;
        p.delayBase  = (rand() % 3) + 2;
        p.speed  = 2.0f + static_cast<float>(rand()) / RAND_MAX * 3.0f;
        p.radius = 0.0f; p.maxRadius = static_cast<float>(maxRadius);
        int colorIndex = rand() % 15;
        p.r = colors[colorIndex][0]; p.g = colors[colorIndex][1]; p.b = colors[colorIndex][2]; p.a = 1.0f;
        p.x = static_cast<float>(startX); p.y = static_cast<float>(startY);
        p.completed = false; p.hasLoggedCompletion = false;
        newFX.particles.push_back(p);
    }
    newFX.startTime = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void FXManager::RenderParticles(FXItem& fxItem) {
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    if (fxItem.type != FXType::ParticleExplosion) return;

    bool allCompleted = true;
    auto now = std::chrono::steady_clock::now();
    float elapsedSecs = std::chrono::duration<float>(now - fxItem.startTime).count();
    float lifeFactor = 1.0f;
    if (fxItem.duration > 0.0f && elapsedSecs > fxItem.duration * 0.7f) {
        lifeFactor = 1.0f - ((elapsedSecs - fxItem.duration * 0.7f) / (fxItem.duration * 0.3f));
        lifeFactor = std::max(0.0f, std::min(1.0f, lifeFactor));
    }

    for (size_t i = 0; i < fxItem.particles.size(); ++i) {
        Particle& p = fxItem.particles[i];
        if (!p.completed) {
            p.delayCount += 1;
            if (p.delayCount >= p.delayBase) {
                p.delayCount = 0;
                p.radius += p.speed;
                if (p.radius >= p.maxRadius) { p.radius = p.maxRadius; p.completed = true; continue; }
            }
            allCompleted = false;
        }
        float sinVal, cosVal;
        FAST_MATH.FastSinCos(p.angle, sinVal, cosVal);
        float xPos = fxItem.originX + cosVal * p.radius;
        float yPos = fxItem.originY + sinVal * p.radius;
        p.x = xPos; p.y = yPos;
        float distanceRatio = p.radius / p.maxRadius;
        float fadeFactor = (1.0f - distanceRatio * distanceRatio) * lifeFactor;
        float alpha = std::max(0.0f, std::min(1.0f, p.a * fadeFactor));
        XMFLOAT4 finalColor(p.r, p.g, p.b, alpha);
        renderer->Blit2DColoredPixel(static_cast<int>(p.x), static_cast<int>(p.y), 2.0f, finalColor);
    }
    if (allCompleted && !fxItem.restartOnExpire) {
        fxItem.progress = 1.0f; fxItem.timeout = 0.0f;
    }
}

// ============================================================
// Render — main 3D-pass render (color fader, starfield, tunnel updates)
// ============================================================

void FXManager::Render() {
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) return;
    if (!renderer) return;
    if (effects.empty() && pendingCallbacks.empty()) return;
    if (bIsRendering.load()) return;

    bIsRendering.store(true);

    try {
        SaveRenderState();

        static auto lastRenderTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::min(std::chrono::duration<float>(now - lastRenderTime).count(), 0.1f);
        lastRenderTime = now;

        // Update animations
        for (auto& fx : effects) {
            if (fx.type == FXType::Starfield)
                UpdateStarfield(deltaTime);
            else if (fx.type == FXType::WarpDotTunnel)
                UpdateWarpDotTunnel(fx, deltaTime);
        }

        // Render effects — DX12: ColorFader, Starfield, WarpDotTunnel are all D2D-backed
        for (auto& fx : effects) {
            if (threadManager.threadVars.bIsShuttingDown.load()) break;
            switch (fx.type) {
            case FXType::ColorFader:
                ApplyColorFader(fx);
                break;
            case FXType::Starfield:
                RenderStarfield(fx);
                break;
            case FXType::WarpDotTunnel:
                RenderWarpDotTunnel(fx);
                break;
            default:
                break;
            }
        }

        // Process pending callbacks
        if (!pendingCallbacks.empty()) {
            ThreadLockHelper callbackLock(threadManager, "fxmanager_callback_process_lock", 500);
            if (callbackLock.IsLocked()) {
                auto currentTime = std::chrono::steady_clock::now();
                std::vector<size_t> callbacksToExecute;
                std::vector<size_t> callbacksToRemove;
                callbacksToExecute.reserve(pendingCallbacks.size());
                callbacksToRemove.reserve(pendingCallbacks.size());

                for (size_t i = 0; i < pendingCallbacks.size(); ++i) {
                    CallbackEntry& entry = pendingCallbacks[i];
                    float age = std::chrono::duration<float>(currentTime - entry.creationTime).count();
                    if (age > 30.0f) { callbacksToRemove.push_back(i); continue; }
                    if (entry.isExecuted) { callbacksToRemove.push_back(i); continue; }
                    for (const auto& fx : effects) {
                        if (fx.fxID == entry.fxID && fx.progress >= 1.0f) {
                            callbacksToExecute.push_back(i); break;
                        }
                    }
                }

                for (size_t index : callbacksToExecute) {
                    if (index < pendingCallbacks.size()) {
                        CallbackEntry& entry = pendingCallbacks[index];
                        if (!entry.isExecuted && entry.callback) {
                            try { entry.callback(); }
                            catch (...) {}
                            entry.isExecuted = true;
                            callbacksToRemove.push_back(index);
                        }
                    }
                }

                std::sort(callbacksToRemove.begin(), callbacksToRemove.end(), std::greater<size_t>());
                callbacksToRemove.erase(std::unique(callbacksToRemove.begin(), callbacksToRemove.end()), callbacksToRemove.end());
                for (size_t index : callbacksToRemove)
                    if (index < pendingCallbacks.size())
                        pendingCallbacks.erase(pendingCallbacks.begin() + index);
            }
        }

        RemoveCompletedEffects();
        RestoreRenderState();
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12FXManager] Exception in Render(): " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
        try { RestoreRenderState(); } catch (...) {}
    }
    catch (...) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12FXManager] Unknown exception in Render()");
        try { RestoreRenderState(); } catch (...) {}
    }

    bIsRendering.store(false);
}

// ============================================================
// Render2D — 2D overlay: scrollers, particles, text scrollers
// ============================================================

void FXManager::Render2D() {
    if (bHasCleanedUp) return;
    static auto lastTweenTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastTweenTime).count();

    UpdateTweens(deltaTime);

    for (auto& fx : effects) {
        if (fx.type == FXType::Scroller)
            ApplyScroller(fx);
        if (fx.type == FXType::ParticleExplosion)
            RenderParticles(fx);
        if (fx.type == FXType::TextScroller) {
            UpdateTextScroller(fx, deltaTime);
            RenderTextScroller(fx);
        }
    }
    lastTweenTime = now;
}

// ============================================================
// Starfield
// ============================================================

void FXManager::CreateStarfield(int numStars, float circularRadius, float resetDepthPos, XMFLOAT3 startPos, bool reverse)
{
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    FXItem newFX;
    newFX.type = FXType::Starfield;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    starfieldID = newFX.fxID;
    newFX.duration = FLT_MAX; newFX.timeout = FLT_MAX; newFX.progress = 0.0f;
    newFX.depthMultiplier = resetDepthPos;
    newFX.starfieldOrigin = startPos;
    newFX.starfieldReverse = reverse;

    for (int i = 0; i < numStars; ++i) {
        Particle p;
        float angle = static_cast<float>(rand()) / RAND_MAX * XM_2PI;
        float dist  = (0.1f + (static_cast<float>(rand()) / RAND_MAX) * 0.9f) * circularRadius;
        float spreadX = cosf(angle) * dist;
        float spreadY = sinf(angle) * dist;

        if (!reverse) {
            p.x = startPos.x + spreadX;
            p.y = startPos.y + spreadY;
            p.angle = startPos.z + resetDepthPos * (0.1f + 0.9f * static_cast<float>(rand()) / RAND_MAX);
            p.vx = 0.0f; p.vy = 0.0f;
        } else {
            float startZ  = 5.0f + static_cast<float>(rand()) / RAND_MAX * (resetDepthPos * 0.1f);
            float fraction = 1.0f - (startZ / resetDepthPos);
            p.vx = spreadX; p.vy = spreadY;
            p.angle = startZ;
            p.x = startPos.x + p.vx * fraction;
            p.y = startPos.y + p.vy * fraction;
        }
        p.speed    = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
        p.radius   = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 2.0f;
        p.maxRadius = resetDepthPos;
        float brightness = 0.7f + static_cast<float>(rand()) / RAND_MAX * 0.3f;
        p.r = brightness;
        p.g = brightness * (0.85f + static_cast<float>(rand()) / RAND_MAX * 0.15f);
        p.b = brightness * (0.9f  + static_cast<float>(rand()) / RAND_MAX * 0.1f);
        p.a = 1.0f;
        p.completed = false; p.hasLoggedCompletion = false;
        p.delayCount = 0; p.delayBase = static_cast<int>(p.angle);
        newFX.particles.push_back(p);
    }
    newFX.startTime = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void FXManager::UpdateStarfield(float deltaTime) {
    for (auto& fx : effects) {
        if (fx.type != FXType::Starfield) continue;
        float    resetDepth = fx.depthMultiplier;
        bool     reverse    = fx.starfieldReverse;
        XMFLOAT3 origin     = fx.starfieldOrigin;

        for (auto& p : fx.particles) {
            if (p.completed) continue;
            float clampedDelta = std::min(deltaTime, 0.1f);
            float zPos = p.angle;

            if (!reverse) {
                zPos -= p.speed * clampedDelta;
                p.a = std::max(0.0f, std::min(1.0f, (zPos / resetDepth) * 1.2f));
                if (zPos <= 5.0f) {
                    float a2 = static_cast<float>(rand()) / RAND_MAX * XM_2PI;
                    float d2 = (0.1f + (static_cast<float>(rand()) / RAND_MAX) * 0.9f) * (resetDepth * 0.1f);
                    float outCos, outSin;
                    FAST_MATH.FastSinCos(a2, outSin, outCos);
                    p.x = origin.x + outCos * d2;
                    p.y = origin.y + outSin * d2;
                    p.angle = origin.z + resetDepth * (0.9f + 0.1f * static_cast<float>(rand()) / RAND_MAX);
                    p.speed  = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
                    p.radius = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 1.2f;
                    p.a = 1.0f;
                } else {
                    p.angle = zPos;
                }
            } else {
                zPos += p.speed * clampedDelta;
                if (zPos >= resetDepth) {
                    float a2 = static_cast<float>(rand()) / RAND_MAX * XM_2PI;
                    float d2 = (0.1f + (static_cast<float>(rand()) / RAND_MAX) * 0.9f) * p.maxRadius;
                    p.vx = cosf(a2) * d2; p.vy = sinf(a2) * d2;
                    p.angle = 5.0f + static_cast<float>(rand()) / RAND_MAX * (resetDepth * 0.1f);
                    float fraction = 1.0f - (p.angle / resetDepth);
                    p.x = origin.x + p.vx * fraction;
                    p.y = origin.y + p.vy * fraction;
                    p.speed  = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
                    p.radius = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 1.2f;
                    p.a = 1.0f;
                } else {
                    float fraction = 1.0f - (zPos / resetDepth);
                    p.x = origin.x + p.vx * fraction;
                    p.y = origin.y + p.vy * fraction;
                    p.angle = zPos;
                    p.a = std::max(0.0f, std::min(1.0f, fraction * 1.2f));
                }
            }
        }
    }
}

void FXManager::StopStarfield() {
    if (starfieldID <= 0) return;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [this](const FXItem& fx) {
            return fx.type == FXType::Starfield && fx.fxID == starfieldID;
        }), effects.end());
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12FXManager] Starfield stopped.");
    starfieldID = 0;
}

// DX12 starfield rendering: identical 3D→screen projection as DX11 but uses renderer abstraction
void FXManager::RenderStarfield(FXItem& fxItem) {
    if (fxItem.type != FXType::Starfield) return;

    XMMATRIX viewProj = renderer->myCamera.GetViewMatrix() * renderer->myCamera.GetProjectionMatrix();
    const float halfW = static_cast<float>(renderer->iOrigWidth)  * 0.5f;
    const float halfH = static_cast<float>(renderer->iOrigHeight) * 0.5f;

    for (auto& p : fxItem.particles) {
        if (p.completed) continue;
        XMVECTOR worldPos = XMVectorSet(p.x, p.y, p.angle, 1.0f);
        XMVECTOR projPos  = XMVector3TransformCoord(worldPos, viewProj);

        if (XMVectorGetZ(projPos) <= 0.0f || XMVectorGetZ(projPos) > 1.0f) continue;
        float ndcX = XMVectorGetX(projPos);
        float ndcY = XMVectorGetY(projPos);
        if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) continue;

        float screenX = (ndcX + 1.0f) * halfW;
        float screenY = (1.0f - ndcY) * halfH;

        float sizeScale = 1.0f + (fxItem.depthMultiplier - p.angle) / fxItem.depthMultiplier * 3.0f;
        float displaySize = p.radius * sizeScale;

        XMFLOAT4 starColor(p.r, p.g, p.b, p.a);
        renderer->Blit2DColoredPixel(static_cast<int>(screenX), static_cast<int>(screenY), displaySize, starColor);
    }
}

// ============================================================
// WarpDotTunnel
// ============================================================

void FXManager::StopAllFX() {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (bIsRendering.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (starfieldID > 0) StopStarfield();
    if (tunnelID    > 0) StopWarpDotTunnel();
    SafelyClearAllEffects();
}

void FXManager::SaveAndSuspendFXForScene() {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (bIsRendering.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    if (!m_sceneSavedEffects.empty() || m_sceneSavedStarfieldID > 0)
        m_sceneSavedEffects.clear();
    m_sceneSavedEffects     = effects;
    m_sceneSavedStarfieldID = starfieldID;
    m_sceneSavedTunnelID    = tunnelID;
    SafelyClearAllEffects();
    starfieldID = 0; tunnelID = 0;
}

void FXManager::RestoreFXAfterScene() {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (bIsRendering.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    if (m_sceneSavedEffects.empty() && m_sceneSavedStarfieldID == 0) return;
    if (tunnelID > 0) StopWarpDotTunnel();
    SafelyClearAllEffects();
    effects     = std::move(m_sceneSavedEffects);
    starfieldID = m_sceneSavedStarfieldID;
    tunnelID    = m_sceneSavedTunnelID;
    m_sceneSavedEffects.clear();
    m_sceneSavedStarfieldID = 0; m_sceneSavedTunnelID = 0;
}

void FXManager::DiscardSavedFXState() {
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    m_sceneSavedEffects.clear();
    m_sceneSavedStarfieldID = 0; m_sceneSavedTunnelID = 0;
}

void FXManager::Init3DWarpDOTTunnel(float x, float y, float z,
    float minRadius, float maxRadius, TunnelSpinCycle spinCycle,
    int travelSpeed, bool reverseTravel, int dotsPerCircle, int density)
{
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    if (tunnelID > 0) StopWarpDotTunnel();

    FXItem newFX;
    newFX.type = FXType::WarpDotTunnel;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    newFX.duration = FLT_MAX; newFX.timeout = FLT_MAX; newFX.progress = 0.0f;
    newFX.startTime = std::chrono::steady_clock::now(); newFX.lastUpdate = newFX.startTime;
    tunnelID = newFX.fxID;

    WarpTunnelData& data = newFX.warpTunnelData;
    data.startX = x; data.startY = y; data.startZ = z;
    data.minRadius = minRadius; data.maxRadius = maxRadius;
    data.spinCycle = spinCycle;
    data.travelSpeed   = std::max(1, travelSpeed);
    data.reverseTravel = reverseTravel;
    data.dotsPerCircle = std::max(3, dotsPerCircle);
    data.density       = std::clamp(density, 1, 100);
    data.totalDistance = 800.0f;
    data.nearZ         = z;
    data.farZ          = z + data.totalDistance;
    data.spinSpeed     = static_cast<float>(travelSpeed) * 0.05f;
    data.smoothLookTarget = XMFLOAT3(x, y, data.farZ);

    int ringCount = data.density;
    data.rings.reserve(ringCount);
    for (int i = 0; i < ringCount; ++i) {
        TunnelRing ring{};
        float fraction = static_cast<float>(i) / static_cast<float>(ringCount);
        ring.zPos = reverseTravel
            ? (data.nearZ + fraction * data.totalDistance)
            : (data.farZ  - fraction * data.totalDistance);
        ring.bornCx = x + WarpTunnelData::kSideWaveRadius * sinf(fraction * XM_2PI);
        ring.bornCy = y + WarpTunnelData::kSideWaveRadius * cosf(fraction * XM_2PI);
        ring.cx = ring.bornCx; ring.cy = ring.bornCy;
        ring.spinAngle = 0.0f; ring.alive = true;
        ring.colorStep = i % WarpTunnelData::kGraySteps;
        data.rings.push_back(ring);
    }

    if (renderer) {
        renderer->myCamera.SetPosition(x, y, data.nearZ);
        renderer->myCamera.SetTarget(XMFLOAT3(x, y, data.farZ));
        renderer->myCamera.SetYawPitch(0.0f, 0.0f);
    }
    effects.push_back(newFX);
}

void FXManager::StopWarpDotTunnel() {
    if (tunnelID <= 0) return;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [this](const FXItem& fx) {
            return fx.type == FXType::WarpDotTunnel && fx.fxID == tunnelID;
        }), effects.end());
    tunnelID = 0;
}

void FXManager::UpdateWarpDotTunnel(FXItem& fx, float deltaTime) {
    WarpTunnelData& data = fx.warpTunnelData;
    if (data.rings.empty()) return;
    const float dt = std::min(deltaTime, 0.05f);
    const float baseSpeed = static_cast<float>(data.travelSpeed);
    data.sideWaveTime += dt;

    for (auto& ring : data.rings) {
        if (!ring.alive) continue;
        float pathT = std::clamp((data.farZ - ring.zPos) / data.totalDistance, 0.0f, 1.0f);
        float t2 = pathT * pathT;
        float speedFactor = data.reverseTravel ? (1.0f + t2*t2*6.0f) : (1.0f + t2*t2*10.0f);
        float frameSpeed = baseSpeed * speedFactor * dt;
        if (!data.reverseTravel) ring.zPos -= frameSpeed;
        else                     ring.zPos += frameSpeed;

        if (!data.reverseTravel && ring.zPos < data.nearZ) {
            ring.zPos = data.farZ;
            float phase = data.sideWaveTime * WarpTunnelData::kSideWaveSpeed;
            ring.bornCx = data.startX + WarpTunnelData::kSideWaveRadius * sinf(phase);
            ring.bornCy = data.startY + WarpTunnelData::kSideWaveRadius * cosf(phase);
        } else if (data.reverseTravel && ring.zPos > data.farZ) {
            ring.zPos = data.nearZ;
            float phase = data.sideWaveTime * WarpTunnelData::kSideWaveSpeed;
            ring.bornCx = data.startX + WarpTunnelData::kSideWaveRadius * sinf(phase);
            ring.bornCy = data.startY + WarpTunnelData::kSideWaveRadius * cosf(phase);
        }
        ring.cx = ring.bornCx; ring.cy = ring.bornCy;

        const float spinDelta = (data.reverseTravel ? -data.spinSpeed : data.spinSpeed) * dt;
        switch (data.spinCycle) {
        case TunnelSpinCycle::Clockwise:     ring.spinAngle += spinDelta; break;
        case TunnelSpinCycle::AntiClockwise: ring.spinAngle -= spinDelta; break;
        default: break;
        }
        ring.spinAngle = fmodf(ring.spinAngle, XM_2PI);
        if (ring.spinAngle < 0.0f) ring.spinAngle += XM_2PI;
    }

    if (renderer) {
        const int ringCount = static_cast<int>(data.rings.size());
        const int lookIdx   = std::min(19, ringCount - 1);
        std::vector<int> order;
        order.reserve(ringCount);
        for (int ri = 0; ri < ringCount; ++ri) order.push_back(ri);
        std::sort(order.begin(), order.end(), [&data](int a, int b) {
            float ptA = (data.farZ - data.rings[a].zPos) / data.totalDistance;
            float ptB = (data.farZ - data.rings[b].zPos) / data.totalDistance;
            return ptA > ptB;
        });
        const TunnelRing& lookRing = data.rings[order[lookIdx]];
        const float alpha = 1.0f - expf(-WarpTunnelData::kCameraSmooth * dt);
        data.smoothLookTarget.x += (lookRing.cx   - data.smoothLookTarget.x) * alpha;
        data.smoothLookTarget.y += (lookRing.cy   - data.smoothLookTarget.y) * alpha;
        data.smoothLookTarget.z += (lookRing.zPos - data.smoothLookTarget.z) * alpha;
        renderer->myCamera.SetTarget(data.smoothLookTarget);
    }
}

// DX12 WarpDotTunnel rendering: identical to DX11 version — uses renderer abstraction
void FXManager::RenderWarpDotTunnel(FXItem& fx) {
    const WarpTunnelData& data = fx.warpTunnelData;
    if (data.rings.empty()) return;

    XMMATRIX viewProj = renderer->myCamera.GetViewMatrix() * renderer->myCamera.GetProjectionMatrix();
    const float angleStep = XM_2PI / static_cast<float>(data.dotsPerCircle);
    const float halfW     = static_cast<float>(renderer->iOrigWidth)  * 0.5f;
    const float halfH     = static_cast<float>(renderer->iOrigHeight) * 0.5f;
    const float edgeFade  = 0.08f;

    static constexpr float kGrayRamp[WarpTunnelData::kGraySteps] = {
        0.08f, 0.19f, 0.30f, 0.44f, 0.58f, 0.72f, 0.86f, 1.0f
    };

    for (const auto& ring : data.rings) {
        if (!ring.alive) continue;
        float pathT = std::clamp((data.farZ - ring.zPos) / data.totalDistance, 0.0f, 1.0f);
        float ringRadius = data.minRadius + (data.maxRadius - data.minRadius) * pathT;

        float alpha = 1.0f;
        if      (pathT < edgeFade)           alpha = pathT / edgeFade;
        else if (pathT > 1.0f - edgeFade)    alpha = (1.0f - pathT) / edgeFade;

        float gray = kGrayRamp[ring.colorStep % WarpTunnelData::kGraySteps];
        float dotSize = 1.0f + pathT * 3.0f;
        XMFLOAT4 dotColor(gray, gray, gray, alpha);

        for (int i = 0; i < data.dotsPerCircle; ++i) {
            float dotAngle = angleStep * static_cast<float>(i) + ring.spinAngle;
            float sinA, cosA;
            FAST_MATH.FastSinCos(dotAngle, sinA, cosA);

            XMVECTOR worldPos = XMVectorSet(
                ring.cx + cosA * ringRadius,
                ring.cy + sinA * ringRadius,
                ring.zPos, 1.0f);

            XMVECTOR proj = XMVector3TransformCoord(worldPos, viewProj);
            float ndcX = XMVectorGetX(proj);
            float ndcY = XMVectorGetY(proj);
            float ndcZ = XMVectorGetZ(proj);

            if (ndcZ <= 0.0f || ndcZ > 1.0f) continue;
            if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) continue;

            float screenX = (ndcX + 1.0f) * halfW;
            float screenY = (1.0f - ndcY) * halfH;

            renderer->Blit2DColoredPixel(
                static_cast<int>(screenX), static_cast<int>(screenY),
                dotSize, dotColor);
        }
    }
}

// ============================================================
// TextScroller (identical to DX_FXManager — all renderer-abstracted)
// ============================================================

void FXManager::CreateTextScrollerLTOR(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float centerHoldTime, float duration, float characterSpacing, float wordSpacing)
{
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    FXItem newFX;
    newFX.type = FXType::TextScroller; newFX.subtype = FXSubType::TXT_SCROLL_LTOR;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    newFX.duration = duration; newFX.timeout = duration + 1.0f; newFX.progress = 0.0f;
    newFX.textScrollData.text = text; newFX.textScrollData.fontName = fontName;
    newFX.textScrollData.fontSize = fontSize; newFX.textScrollData.textColor = textColor;
    newFX.textScrollData.scrollSpeed = scrollSpeed; newFX.textScrollData.centerHoldTime = centerHoldTime;
    newFX.textScrollData.centerHoldTimer = 0.0f;
    newFX.textScrollData.regionX = regionX; newFX.textScrollData.regionY = regionY;
    newFX.textScrollData.regionWidth = regionWidth; newFX.textScrollData.regionHeight = regionHeight;
    newFX.textScrollData.currentXPosition = regionX - 100.0f;
    newFX.textScrollData.currentYPosition = regionY + (regionHeight / 2.0f);
    newFX.textScrollData.isInCenterPhase = false; newFX.textScrollData.hasReachedCenter = false;
    newFX.startTime = std::chrono::steady_clock::now(); newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void FXManager::CreateTextScrollerRTOL(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float centerHoldTime, float duration, float characterSpacing, float wordSpacing)
{
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    FXItem newFX;
    newFX.type = FXType::TextScroller; newFX.subtype = FXSubType::TXT_SCROLL_RTOL;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    newFX.duration = duration; newFX.timeout = duration + 1.0f; newFX.progress = 0.0f;
    newFX.textScrollData.text = text; newFX.textScrollData.fontName = fontName;
    newFX.textScrollData.fontSize = fontSize; newFX.textScrollData.textColor = textColor;
    newFX.textScrollData.scrollSpeed = scrollSpeed; newFX.textScrollData.centerHoldTime = centerHoldTime;
    newFX.textScrollData.centerHoldTimer = 0.0f;
    newFX.textScrollData.regionX = regionX; newFX.textScrollData.regionY = regionY;
    newFX.textScrollData.regionWidth = regionWidth; newFX.textScrollData.regionHeight = regionHeight;
    newFX.textScrollData.currentXPosition = regionX + regionWidth + 100.0f;
    newFX.textScrollData.currentYPosition = regionY + (regionHeight / 2.0f);
    newFX.textScrollData.isInCenterPhase = false; newFX.textScrollData.hasReachedCenter = false;
    newFX.startTime = std::chrono::steady_clock::now(); newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void FXManager::CreateTextScrollerConsistent(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float duration, float characterSpacing, float wordSpacing)
{
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    FXItem newFX;
    newFX.type = FXType::TextScroller; newFX.subtype = FXSubType::TXT_SCROLL_CONSISTANT;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    newFX.duration = duration; newFX.timeout = duration == FLT_MAX ? FLT_MAX : duration + 1.0f;
    newFX.progress = 0.0f;
    newFX.textScrollData.text = text; newFX.textScrollData.fontName = fontName;
    newFX.textScrollData.fontSize = fontSize; newFX.textScrollData.textColor = textColor;
    newFX.textScrollData.scrollSpeed = scrollSpeed;
    newFX.textScrollData.characterSpacing = characterSpacing; newFX.textScrollData.wordSpacing = wordSpacing;
    newFX.textScrollData.regionX = regionX; newFX.textScrollData.regionY = regionY;
    newFX.textScrollData.regionWidth = regionWidth; newFX.textScrollData.regionHeight = regionHeight;
    newFX.textScrollData.currentXPosition = regionX + regionWidth;
    newFX.textScrollData.currentYPosition = regionY + (regionHeight / 2.0f);
    newFX.startTime = std::chrono::steady_clock::now(); newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void FXManager::CreateTextScrollerMovie(const std::vector<std::wstring>& textLines, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float lineSpacing, float duration, float characterSpacing, float wordSpacing)
{
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    FXItem newFX;
    newFX.type = FXType::TextScroller; newFX.subtype = FXSubType::TXT_SCROLL_MOVIE;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    newFX.duration = duration; newFX.timeout = duration + 1.0f; newFX.progress = 0.0f;
    newFX.textScrollData.textLines = textLines;
    newFX.textScrollData.fontSize = fontSize; newFX.textScrollData.textColor = textColor;
    newFX.textScrollData.scrollSpeed = scrollSpeed; newFX.textScrollData.lineSpacing = lineSpacing;
    newFX.textScrollData.regionX = regionX; newFX.textScrollData.regionY = regionY;
    newFX.textScrollData.regionWidth = regionWidth; newFX.textScrollData.regionHeight = regionHeight;
    newFX.textScrollData.currentYPosition = regionY + regionHeight;
    newFX.textScrollData.currentLineIndex = 0;
    newFX.startTime = std::chrono::steady_clock::now(); newFX.lastUpdate = newFX.startTime;
    effects.push_back(newFX);
}

void FXManager::StopTextScroller(int effectID) {
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [effectID](const FXItem& fx) {
            return fx.type == FXType::TextScroller && fx.fxID == effectID;
        }), effects.end());
}

void FXManager::PauseTextScroller(int effectID) {
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    for (auto& fx : effects)
        if (fx.type == FXType::TextScroller && fx.fxID == effectID)
            fx.isPaused = true;
}

void FXManager::ResumeTextScroller(int effectID) {
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) return;
    for (auto& fx : effects)
        if (fx.type == FXType::TextScroller && fx.fxID == effectID) {
            fx.isPaused = false;
            fx.lastUpdate = std::chrono::steady_clock::now();
        }
}

void FXManager::UpdateTextScroller(FXItem& fxItem, float deltaTime) {
    if (fxItem.isPaused || fxItem.type != FXType::TextScroller) return;

    switch (fxItem.subtype) {
    case FXSubType::TXT_SCROLL_LTOR: {
        if (!fxItem.textScrollData.widthCached) {
            fxItem.textScrollData.cachedTextWidth = renderer->CalculateTextWidth(
                fxItem.textScrollData.text, fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
            fxItem.textScrollData.widthCached = true;
        }
        float centerX   = fxItem.textScrollData.regionX + (fxItem.textScrollData.regionWidth / 2.0f);
        float textCenterX = centerX - (fxItem.textScrollData.cachedTextWidth / 2.0f);
        if (!fxItem.textScrollData.hasReachedCenter) {
            float distanceToCenter = fabsf(fxItem.textScrollData.currentXPosition - textCenterX);
            float maxDistance = fxItem.textScrollData.regionWidth / 2.0f;
            float speedMultiplier = 1.0f + (1.0f - (distanceToCenter / maxDistance)) * 2.0f;
            fxItem.textScrollData.currentXPosition += fxItem.textScrollData.scrollSpeed * speedMultiplier * deltaTime;
            if (fxItem.textScrollData.currentXPosition >= textCenterX) {
                fxItem.textScrollData.currentXPosition = textCenterX;
                fxItem.textScrollData.hasReachedCenter = true;
                fxItem.textScrollData.isInCenterPhase  = true;
                fxItem.textScrollData.centerHoldTimer  = 0.0f;
            }
        } else if (fxItem.textScrollData.isInCenterPhase) {
            fxItem.textScrollData.centerHoldTimer += deltaTime;
            if (fxItem.textScrollData.centerHoldTimer >= fxItem.textScrollData.centerHoldTime)
                fxItem.textScrollData.isInCenterPhase = false;
        } else {
            float distanceFromCenter = fabsf(fxItem.textScrollData.currentXPosition - textCenterX);
            float maxDistance = fxItem.textScrollData.regionWidth / 2.0f;
            float speedMultiplier = 1.0f + (distanceFromCenter / maxDistance) * 2.0f;
            fxItem.textScrollData.currentXPosition += fxItem.textScrollData.scrollSpeed * speedMultiplier * deltaTime;
            if (fxItem.textScrollData.currentXPosition > fxItem.textScrollData.regionX + fxItem.textScrollData.regionWidth + 100.0f)
                fxItem.progress = 1.0f;
        }
        break;
    }
    case FXSubType::TXT_SCROLL_RTOL: {
        if (!fxItem.textScrollData.widthCached) {
            fxItem.textScrollData.cachedTextWidth = renderer->CalculateTextWidth(
                fxItem.textScrollData.text, fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
            fxItem.textScrollData.widthCached = true;
        }
        float centerX   = fxItem.textScrollData.regionX + (fxItem.textScrollData.regionWidth / 2.0f);
        float textCenterX = centerX - (fxItem.textScrollData.cachedTextWidth / 2.0f);
        if (!fxItem.textScrollData.hasReachedCenter) {
            float distanceToCenter = fabsf(fxItem.textScrollData.currentXPosition - textCenterX);
            float maxDistance = fxItem.textScrollData.regionWidth / 2.0f;
            float speedMultiplier = 1.0f + (1.0f - (distanceToCenter / maxDistance)) * 2.0f;
            fxItem.textScrollData.currentXPosition -= fxItem.textScrollData.scrollSpeed * speedMultiplier * deltaTime;
            if (fxItem.textScrollData.currentXPosition <= textCenterX) {
                fxItem.textScrollData.currentXPosition = textCenterX;
                fxItem.textScrollData.hasReachedCenter = true;
                fxItem.textScrollData.isInCenterPhase  = true;
                fxItem.textScrollData.centerHoldTimer  = 0.0f;
            }
        } else if (fxItem.textScrollData.isInCenterPhase) {
            fxItem.textScrollData.centerHoldTimer += deltaTime;
            if (fxItem.textScrollData.centerHoldTimer >= fxItem.textScrollData.centerHoldTime)
                fxItem.textScrollData.isInCenterPhase = false;
        } else {
            float distanceFromCenter = fabsf(fxItem.textScrollData.currentXPosition - textCenterX);
            float maxDistance = fxItem.textScrollData.regionWidth / 2.0f;
            float speedMultiplier = 1.0f + (distanceFromCenter / maxDistance) * 2.0f;
            fxItem.textScrollData.currentXPosition -= fxItem.textScrollData.scrollSpeed * speedMultiplier * deltaTime;
            if (fxItem.textScrollData.currentXPosition < fxItem.textScrollData.regionX - 100.0f)
                fxItem.progress = 1.0f;
        }
        break;
    }
    case FXSubType::TXT_SCROLL_CONSISTANT: {
        if (!fxItem.textScrollData.widthCached) {
            const auto& text = fxItem.textScrollData.text;
            const auto& fontName = fxItem.textScrollData.fontName;
            const float fSize = fxItem.textScrollData.fontSize;
            const float cSpace = fxItem.textScrollData.characterSpacing;
            const float wSpace = fxItem.textScrollData.wordSpacing;
            fxItem.textScrollData.cachedCharWidths.resize(text.length());
            fxItem.textScrollData.cachedCharOffsets.resize(text.length());
            float offset = 0.0f;
            for (size_t i = 0; i < text.length(); ++i) {
                float w = renderer->GetCharacterWidth(text[i], fSize, fontName) + cSpace;
                if (text[i] == L' ') w += wSpace;
                fxItem.textScrollData.cachedCharOffsets[i] = offset;
                fxItem.textScrollData.cachedCharWidths[i] = w;
                offset += w;
            }
            fxItem.textScrollData.cachedTotalTextWidth = offset;
            fxItem.textScrollData.widthCached = true;
        }
        fxItem.textScrollData.currentXPosition -= fxItem.textScrollData.scrollSpeed * deltaTime;
        float totalTextWidth = fxItem.textScrollData.cachedTotalTextWidth;
        if (fxItem.textScrollData.currentXPosition + totalTextWidth < fxItem.textScrollData.regionX)
            fxItem.textScrollData.currentXPosition = fxItem.textScrollData.regionX + fxItem.textScrollData.regionWidth;
        if (fxItem.duration != FLT_MAX) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - fxItem.startTime).count();
            if (elapsed >= fxItem.duration) fxItem.progress = 1.0f;
        }
        break;
    }
    case FXSubType::TXT_SCROLL_MOVIE: {
        fxItem.textScrollData.currentYPosition -= fxItem.textScrollData.scrollSpeed * deltaTime;
        float totalHeight = fxItem.textScrollData.textLines.size() * fxItem.textScrollData.lineSpacing;
        if (fxItem.textScrollData.currentYPosition + totalHeight < fxItem.textScrollData.regionY)
            fxItem.progress = 1.0f;
        break;
    }
    default: break;
    }
}

void FXManager::RenderTextScroller(FXItem& fxItem) {
    if (fxItem.type != FXType::TextScroller) return;

    switch (fxItem.subtype) {
    case FXSubType::TXT_SCROLL_LTOR:
    case FXSubType::TXT_SCROLL_RTOL: {
        float transparency = 1.0f;
        if (!fxItem.textScrollData.isInCenterPhase) {
            float centerX = fxItem.textScrollData.regionX + (fxItem.textScrollData.regionWidth / 2.0f);
            float distanceFromCenter = fabsf(fxItem.textScrollData.currentXPosition - centerX);
            float fadeDistance = fxItem.textScrollData.regionWidth / 4.0f;
            if (distanceFromCenter > fadeDistance)
                transparency = std::max(0.0f, 1.0f - ((distanceFromCenter - fadeDistance) / fadeDistance));
        }
        XMFLOAT4 renderColor = fxItem.textScrollData.textColor;
        renderColor.w *= transparency;
        MyColor color(
            static_cast<uint8_t>(renderColor.x * 255.0f),
            static_cast<uint8_t>(renderColor.y * 255.0f),
            static_cast<uint8_t>(renderColor.z * 255.0f),
            static_cast<uint8_t>(renderColor.w * 255.0f));
        Vector2 position(fxItem.textScrollData.currentXPosition, fxItem.textScrollData.currentYPosition);
        renderer->DrawMyText(fxItem.textScrollData.text, position, color, fxItem.textScrollData.fontSize);
        break;
    }
    case FXSubType::TXT_SCROLL_CONSISTANT: {
        const float baseX = fxItem.textScrollData.currentXPosition;
        const float baseY = fxItem.textScrollData.currentYPosition;
        const float regionLeft  = fxItem.textScrollData.regionX;
        const float regionRight = regionLeft + fxItem.textScrollData.regionWidth;
        const float fadeDistance = 100.0f;
        const auto& text     = fxItem.textScrollData.text;
        const auto& offsets  = fxItem.textScrollData.cachedCharOffsets;
        const auto& widths   = fxItem.textScrollData.cachedCharWidths;
        const float fontSize = fxItem.textScrollData.fontSize;
        const auto& fontName = fxItem.textScrollData.fontName;
        const XMFLOAT4& baseColor = fxItem.textScrollData.textColor;

        for (size_t i = 0; i < text.length(); ++i) {
            float charX = baseX + offsets[i];
            if (charX < regionLeft - fadeDistance - 50.0f || charX > regionRight + fadeDistance + 50.0f) continue;
            float charCenterX = charX + (widths[i] * 0.5f);
            float transparency = CalculateCharacterTransparency(charCenterX, regionLeft, regionRight, fadeDistance);
            if (transparency <= 0.01f) continue;
            XMFLOAT4 renderColor = baseColor;
            renderColor.w *= transparency;
            MyColor color(
                static_cast<uint8_t>(renderColor.x * 255.0f),
                static_cast<uint8_t>(renderColor.y * 255.0f),
                static_cast<uint8_t>(renderColor.z * 255.0f),
                static_cast<uint8_t>(renderColor.w * 255.0f));
            renderer->DrawMyTextWithFont(std::wstring(1, text[i]), Vector2(charX, baseY), color, fontSize, fontName);
        }
        break;
    }
    case FXSubType::TXT_SCROLL_MOVIE: {
        if (!fxItem.textScrollData.widthCached) {
            const auto& lines = fxItem.textScrollData.textLines;
            fxItem.textScrollData.cachedLineWidths.resize(lines.size());
            for (size_t i = 0; i < lines.size(); ++i)
                fxItem.textScrollData.cachedLineWidths[i] = renderer->CalculateTextWidth(
                    lines[i], fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
            fxItem.textScrollData.widthCached = true;
        }
        const float lineY   = fxItem.textScrollData.currentYPosition;
        const float regionTop = fxItem.textScrollData.regionY;
        const float regionBot = regionTop + fxItem.textScrollData.regionHeight;
        const float regionX  = fxItem.textScrollData.regionX;
        const float regionW  = fxItem.textScrollData.regionWidth;
        const float fontSize = fxItem.textScrollData.fontSize;
        const XMFLOAT4& baseColor = fxItem.textScrollData.textColor;
        for (size_t i = 0; i < fxItem.textScrollData.textLines.size(); ++i) {
            float currentLineY = lineY + (i * fxItem.textScrollData.lineSpacing);
            float transparency = CalculateTextTransparency(currentLineY, regionTop, regionBot, 50.0f);
            if (transparency <= 0.0f) continue;
            XMFLOAT4 renderColor = baseColor;
            renderColor.w *= transparency;
            MyColor color(
                static_cast<uint8_t>(renderColor.x * 255.0f),
                static_cast<uint8_t>(renderColor.y * 255.0f),
                static_cast<uint8_t>(renderColor.z * 255.0f),
                static_cast<uint8_t>(renderColor.w * 255.0f));
            float centeredX = regionX + (regionW - fxItem.textScrollData.cachedLineWidths[i]) * 0.5f;
            renderer->DrawMyText(fxItem.textScrollData.textLines[i], Vector2(centeredX, currentLineY), color, fontSize);
        }
        break;
    }
    default: break;
    }
}

// ============================================================
// TextScroller helpers
// ============================================================

float FXManager::CalculateTextTransparency(float position, float regionStart, float regionEnd, float fadeDistance) {
    if (position < regionStart - fadeDistance || position > regionEnd + fadeDistance) return 0.0f;
    if (position < regionStart) return 1.0f - (regionStart - position) / fadeDistance;
    if (position > regionEnd)   return 1.0f - (position - regionEnd)   / fadeDistance;
    return 1.0f;
}

float FXManager::CalculateCharacterTransparency(float charPosition, float regionStart, float regionEnd, float fadeDistance) {
    if (charPosition < regionStart - fadeDistance || charPosition > regionEnd + fadeDistance) return 0.0f;
    if (charPosition > regionEnd) {
        float d = charPosition - regionEnd;
        return std::max(0.0f, std::min(1.0f, 1.0f - d / fadeDistance));
    }
    if (charPosition < regionStart) {
        float d = regionStart - charPosition;
        return std::max(0.0f, std::min(1.0f, 1.0f - d / fadeDistance));
    }
    float regionWidth = regionEnd - regionStart;
    float positionInRegion = (charPosition - regionStart) / regionWidth;
    const float edgeFadePercent = 0.25f;
    if (positionInRegion < edgeFadePercent)
        return std::max(0.0f, std::min(1.0f, positionInRegion / edgeFadePercent));
    if (positionInRegion > (1.0f - edgeFadePercent)) {
        float distanceFromRightEdge = 1.0f - positionInRegion;
        return std::max(0.0f, std::min(1.0f, distanceFromRightEdge / edgeFadePercent));
    }
    return 1.0f;
}

float FXManager::CalculateTextWidthWithSpacing(const std::wstring& text, const std::wstring& fontName,
    float fontSize, float characterSpacing, float wordSpacing)
{
    float totalWidth = 0.0f;
    for (wchar_t character : text) {
        float charWidth = renderer->GetCharacterWidth(character, fontSize, fontName) + characterSpacing;
        if (character == L' ') charWidth += wordSpacing;
        totalWidth += charWidth;
    }
    return totalWidth;
}

void FXManager::SplitTextIntoLines(const std::wstring& text, std::vector<std::wstring>& lines, float maxWidth, float fontSize) {
    lines.clear();
    std::wstringstream ss(text);
    std::wstring word, currentLine;
    while (std::getline(ss, word, L' ')) {
        std::wstring testLine = currentLine.empty() ? word : currentLine + L" " + word;
        float lineWidth = renderer->CalculateTextWidth(testLine, fontSize, 1000.0f);
        if (lineWidth > maxWidth && !currentLine.empty()) {
            lines.push_back(currentLine);
            currentLine = word;
        } else {
            currentLine = testLine;
        }
    }
    if (!currentLine.empty()) lines.push_back(currentLine);
}

// ============================================================
// TextFadeInOut (ShowLoadingText / StopLoadingText / RenderLoadingText)
// ============================================================

int FXManager::ShowLoadingText(const std::wstring& text, XMFLOAT4 endColor, float fadeInDuration,
    float fadeOutDuration, XMFLOAT4 startColor, float posX, float posY, const TextRenderStyle* fontStyle)
{
    if (bHasCleanedUp) return -1;

    float maxRemainingFadeOut = 0.0f;
    for (auto& fx : effects) {
        if (fx.type != FXType::TextFadeInOut) continue;
        if (fx.textFadeData.immediateStop) continue;
        auto& d = fx.textFadeData;

        if (d.pendingDelay > 0.0f) { d.immediateStop = true; fx.progress = 1.0f; continue; }

        if (d.phase == TextFadePhase::FadeIn) {
            float t = (d.fadeInDuration > 0.0f) ? std::clamp(d.phaseTimer / d.fadeInDuration, 0.0f, 1.0f) : 1.0f;
            d.fadeOutStartColor = {
                d.startColor.x + (d.endColor.x - d.startColor.x) * t,
                d.startColor.y + (d.endColor.y - d.startColor.y) * t,
                d.startColor.z + (d.endColor.z - d.startColor.z) * t,
                d.startColor.w + (d.endColor.w  - d.startColor.w) * t
            };
            float remaining = d.fadeOutDuration;
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
            if (remaining > maxRemainingFadeOut) maxRemainingFadeOut = remaining;
        } else if (d.phase == TextFadePhase::Holding) {
            d.fadeOutStartColor = d.endColor;
            float remaining = d.fadeOutDuration;
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
            if (remaining > maxRemainingFadeOut) maxRemainingFadeOut = remaining;
        } else if (d.phase == TextFadePhase::FadeOut) {
            float remaining = d.fadeOutDuration - d.phaseTimer;
            if (remaining > maxRemainingFadeOut) maxRemainingFadeOut = remaining;
        }
    }

    static int nextID = 5000;
    int newID = nextID++;

    FXItem fx{};
    fx.fxID = newID; fx.nextEffectID = -1;
    fx.type = FXType::TextFadeInOut; fx.subtype = FXSubType::TXT_FADE_IN;
    fx.duration = 0.0f; fx.progress = 0.0f; fx.delay = 0.0f; fx.timeout = 0.0f;
    fx.startTime = std::chrono::steady_clock::now(); fx.lastUpdate = fx.startTime;

    TextFadeData& d = fx.textFadeData;
    d.text = text; d.startColor = startColor; d.endColor = endColor;
    d.fadeOutColor = XMFLOAT4{ 0.0f, 0.0f, 0.0f, 0.0f };
    d.fadeInDuration  = std::max(fadeInDuration,  0.05f);
    d.fadeOutDuration = std::max(fadeOutDuration, 0.05f);
    d.displayDuration = -1.0f; d.pendingDelay = maxRemainingFadeOut;
    d.phase = TextFadePhase::FadeIn; d.phaseTimer = 0.0f; d.immediateStop = false;

    d.posX = (posX < 0.0f) ? 20.0f : posX;
    d.posY = (posY < 0.0f) ? (renderer ? static_cast<float>(renderer->iOrigHeight) * LOADER_TEXT_Y_RATIO
                                        : fDEFAULT_WINDOW_HEIGHT * LOADER_TEXT_Y_RATIO) : posY;
    if (fontStyle) d.fontStyle = *fontStyle;

    effects.push_back(std::move(fx));
    return newID;
}

void FXManager::StopLoadingText() {
    for (auto& fx : effects) {
        if (fx.type != FXType::TextFadeInOut) continue;
        if (fx.textFadeData.immediateStop) continue;
        auto& d = fx.textFadeData;
        if (d.pendingDelay > 0.0f || d.phase == TextFadePhase::Stopped) {
            d.immediateStop = true; fx.progress = 1.0f; continue;
        }
        if (d.phase == TextFadePhase::FadeIn) {
            float t = (d.fadeInDuration > 0.0f) ? std::clamp(d.phaseTimer / d.fadeInDuration, 0.0f, 1.0f) : 1.0f;
            d.fadeOutStartColor = {
                d.startColor.x + (d.endColor.x - d.startColor.x) * t,
                d.startColor.y + (d.endColor.y - d.startColor.y) * t,
                d.startColor.z + (d.endColor.z - d.startColor.z) * t,
                d.startColor.w + (d.endColor.w  - d.startColor.w) * t
            };
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
        } else if (d.phase == TextFadePhase::Holding) {
            d.fadeOutStartColor = d.endColor;
            d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
        }
    }
}

void FXManager::RenderLoadingText() {
    if (bHasCleanedUp || !renderer) return;
    static auto lastRLT = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::min(std::chrono::duration<float>(now - lastRLT).count(), 0.1f);
    lastRLT = now;
    for (auto& fx : effects) {
        if (fx.type != FXType::TextFadeInOut) continue;
        UpdateTextFadeInOut(fx, deltaTime);
        if (fx.textFadeData.phase != TextFadePhase::Stopped && !fx.textFadeData.immediateStop)
            RenderTextFadeInOut(fx);
    }
}

void FXManager::UpdateTextFadeInOut(FXItem& fx, float deltaTime) {
    TextFadeData& d = fx.textFadeData;
    if (d.immediateStop) { fx.progress = 1.0f; return; }
    if (d.pendingDelay > 0.0f) { d.pendingDelay -= deltaTime; return; }
    d.phaseTimer += deltaTime;
    switch (d.phase) {
    case TextFadePhase::FadeIn:
        if (d.phaseTimer >= d.fadeInDuration) { d.phase = TextFadePhase::Holding; d.phaseTimer = 0.0f; }
        break;
    case TextFadePhase::Holding:
        if (d.displayDuration >= 0.0f && d.phaseTimer >= d.displayDuration) {
            d.fadeOutStartColor = d.endColor; d.phase = TextFadePhase::FadeOut; d.phaseTimer = 0.0f;
        }
        break;
    case TextFadePhase::FadeOut:
        if (d.phaseTimer >= d.fadeOutDuration) { d.phase = TextFadePhase::Stopped; fx.progress = 1.0f; }
        break;
    case TextFadePhase::Stopped:
        fx.progress = 1.0f;
        break;
    }
}

void FXManager::RenderTextFadeInOut(FXItem& fx) {
    if (!renderer) return;
    TextFadeData& d = fx.textFadeData;
    if (d.text.empty() || d.pendingDelay > 0.0f) return;

    XMFLOAT4 renderColor = d.endColor;
    float alpha = renderColor.w;

    switch (d.phase) {
    case TextFadePhase::FadeIn: {
        float t = (d.fadeInDuration > 0.0f) ? std::clamp(d.phaseTimer / d.fadeInDuration, 0.0f, 1.0f) : 1.0f;
        renderColor.x = d.startColor.x + (d.endColor.x - d.startColor.x) * t;
        renderColor.y = d.startColor.y + (d.endColor.y - d.startColor.y) * t;
        renderColor.z = d.startColor.z + (d.endColor.z - d.startColor.z) * t;
        alpha         = d.startColor.w + (d.endColor.w  - d.startColor.w) * t;
        break;
    }
    case TextFadePhase::Holding:
        renderColor = d.endColor; alpha = d.endColor.w; break;
    case TextFadePhase::FadeOut: {
        float t = (d.fadeOutDuration > 0.0f) ? std::clamp(d.phaseTimer / d.fadeOutDuration, 0.0f, 1.0f) : 1.0f;
        renderColor.x = d.fadeOutStartColor.x + (d.fadeOutColor.x - d.fadeOutStartColor.x) * t;
        renderColor.y = d.fadeOutStartColor.y + (d.fadeOutColor.y - d.fadeOutStartColor.y) * t;
        renderColor.z = d.fadeOutStartColor.z + (d.fadeOutColor.z - d.fadeOutStartColor.z) * t;
        alpha         = d.fadeOutStartColor.w + (d.fadeOutColor.w  - d.fadeOutStartColor.w) * t;
        break;
    }
    default: return;
    }

    renderColor.w = std::clamp(alpha, 0.0f, 1.0f);
    if (renderColor.w < 0.005f) return;

    uint8_t r = static_cast<uint8_t>(std::clamp(renderColor.x, 0.0f, 1.0f) * 255.0f);
    uint8_t g = static_cast<uint8_t>(std::clamp(renderColor.y, 0.0f, 1.0f) * 255.0f);
    uint8_t b = static_cast<uint8_t>(std::clamp(renderColor.z, 0.0f, 1.0f) * 255.0f);
    uint8_t a = static_cast<uint8_t>(renderColor.w * 255.0f);

    renderer->DrawMyTextStyled(d.text, Vector2(d.posX, d.posY), MyColor(r, g, b, a), d.fontStyle);
}

bool FXManager::HasActiveLoadingTextEffects() const {
    for (const auto& fx : effects) {
        if (fx.type != FXType::TextFadeInOut) continue;
        if (fx.textFadeData.immediateStop) continue;
        if (fx.textFadeData.phase == TextFadePhase::Stopped) continue;
        return true;
    }
    return false;
}

#pragma warning(pop)

#endif // _WIN32 || _WIN64
#endif // __USE_DIRECTX_12__
