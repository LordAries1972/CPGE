// ---------------------------------------------------------------------------------------------------------------
// VULKAN_FXManager.h  —  Visual Effects Manager for the Vulkan Renderer
// ---------------------------------------------------------------------------------------------------------------
// Vulkan equivalent of DX_FXManager.h.
// Replaces all D3D11 types with Vulkan equivalents:
//   ID3D11DeviceContext*  →  VkCommandBuffer
//   ID3D11BlendState*     →  VkPipeline (per-effect blend pipeline)
//   XMMATRIX              →  per-platform matrix type (DirectXMath / GLM)
//
// Platform guards:
//   #if defined(PLATFORM_WINDOWS)  /  #elif defined(PLATFORM_LINUX)  /  #elif defined(PLATFORM_ANDROID)
// ---------------------------------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#include "Renderer.h"

#if defined(__USE_VULKAN__)

#include <vulkan/vulkan.h>

#if defined(PLATFORM_WINDOWS)
    #include <vulkan/vulkan_win32.h>
    #include <DirectXMath.h>
    #include <glm/glm.hpp>                  // VulkanCamera returns glm types on all platforms
    #include <glm/gtc/matrix_transform.hpp>
    using namespace DirectX;
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    #include <glm/glm.hpp>
    #include <glm/gtc/matrix_transform.hpp>
    // Map XMFLOAT3/4 to GLM for shared struct compatibility
    using XMFLOAT3 = glm::vec3;
    using XMFLOAT4 = glm::vec4;
    using XMMATRIX = glm::mat4;
#endif

class Debug;
extern std::shared_ptr<Renderer> renderer;

// ---------------------------------------------------------------------------------------------------------------
// FX type enumerations (identical to DX_FXManager — shared by engine)
// ---------------------------------------------------------------------------------------------------------------
enum class FXType {
    ColorFader,
    PixelOut,
    ScreenWipe,
    Scroller,
    ParticleExplosion,
    Starfield,
    TextScroller,
    WarpDotTunnel,
    TextFadeInOut,                                                              // Loading-screen text with per-frame fade in / fade out
};

enum class FXSubType {
    FadeIntoColor, FadeToBackground, FadeToTargetColor,
    WipeRight, WipeLeft, WipeUp, WipeDown,
    ScrollRight, ScrollLeft, ScrollUp, ScrollDown,
    ScrollUpAndLeft, ScrollUpAndRight, ScrollDownAndLeft, ScrollDownAndRight,
    TXT_SCROLL_LTOR, TXT_SCROLL_RTOL, TXT_SCROLL_CONSISTANT, TXT_SCROLL_MOVIE,
    TXT_FADE_IN,                                                                // Fade text in from startColor → endColor
    TXT_FADE_OUT,                                                               // Fade text out from endColor → fadeOutColor
};

// Spin direction for the WarpDotTunnel rings
enum class TunnelSpinCycle {
    None,
    Clockwise,
    AntiClockwise,
};

// ---------------------------------------------------------------------------------------------------------------
// Shared data structures (same fields as DX_FXManager — only Vulkan-specific members differ)
// ---------------------------------------------------------------------------------------------------------------
struct VKParticle {
    float x, y;
    float r, g, b, a;
    float angle;
    float speed;
    float radius;
    float maxRadius;
    float vx = 0.0f, vy = 0.0f;
    int   delayBase = 0, delayCount = 0;
    bool  completed = false;
    bool  hasLoggedCompletion = false;
};

struct VKStar {
    XMFLOAT3 position;
    XMFLOAT4 color;
    float     size;
    float     speed;
    bool      active;
};

// One ring of dots in a WarpDotTunnel effect
struct VKTunnelRing {
    float zPos      = 0.0f;
    float spinAngle = 0.0f;
    float cx        = 0.0f;     // current X centre (set at birth, never changes mid-flight)
    float cy        = 0.0f;     // current Y centre (set at birth, never changes mid-flight)
    float bornCx    = 0.0f;     // X position assigned at birth; ring flies straight from here
    float bornCy    = 0.0f;     // Y position assigned at birth; ring flies straight from here
    bool  alive     = true;
    int   colorStep = 0;
};

// Per-effect state for WarpDotTunnel
struct VKWarpTunnelData {
    float            startX        = 0.0f;
    float            startY        = 0.0f;
    float            startZ        = 0.0f;
    float            minRadius     = 5.0f;
    float            maxRadius     = 200.0f;
    TunnelSpinCycle  spinCycle     = TunnelSpinCycle::None;
    int              travelSpeed   = 80;
    bool             reverseTravel = false;
    int              dotsPerCircle = 24;
    int              density       = 4;

    float totalDistance = 800.0f;
    float nearZ         = 0.0f;
    float farZ          = 800.0f;
    float spinSpeed       = 0.0f;
    float pathPhaseOffset = 0.0f;    // advances each frame so the path drifts, creating winding movement
    float sideWaveTime    = 0.0f;    // accumulates per-frame; drives the furthest-ring left/right sway
    XMFLOAT3 smoothLookTarget = { 0.0f, 0.0f, 800.0f }; // exponentially-smoothed camera look target

    static constexpr float kSideWaveRadius = 60.0f;
    static constexpr float kSideWaveSpeed  = 0.5f;
    static constexpr float kCameraSmooth   = 3.0f;
    static constexpr int   kGraySteps      = 8;
    static constexpr float kMaxXYRadius = 300.0f;

    std::vector<VKTunnelRing> rings;
};

struct VKTextScrollData {
    std::wstring text;
    std::vector<std::wstring> textLines;
    std::wstring fontName;
    float fontSize;
    XMFLOAT4 textColor;
    float scrollSpeed;
    float currentXPosition;
    float currentYPosition;
    float centerHoldTime;
    float centerHoldTimer;
    float regionWidth, regionHeight, regionX, regionY;
    int   currentLineIndex;
    float lineSpacing, characterSpacing, wordSpacing;
    bool  isInCenterPhase, hasReachedCenter;
    bool  widthCached = false;
    float cachedTextWidth = 0.0f;
    float cachedTotalTextWidth = 0.0f;
    std::vector<float> cachedCharWidths;
    std::vector<float> cachedCharOffsets;
    std::vector<float> cachedLineWidths;

    VKTextScrollData() :
        fontName(L"Arial"), fontSize(16.0f), textColor{1,1,1,1},
        scrollSpeed(1.0f), currentXPosition(0), currentYPosition(0),
        centerHoldTime(2.0f), centerHoldTimer(0), regionWidth(800), regionHeight(600),
        regionX(0), regionY(0), currentLineIndex(0), lineSpacing(20),
        characterSpacing(1.0f), wordSpacing(8.0f), isInCenterPhase(false), hasReachedCenter(false) {}
};

// Phase state machine for TextFadeInOut
enum class TextFadePhase { FadeIn, Holding, FadeOut, Stopped };

// Default loading-screen font resolved per platform at compile time
namespace LoadingTextFX {
#if   defined(PLATFORM_WINDOWS) || defined(_WIN32) || defined(_WIN64)
    static constexpr const wchar_t* kFontName = L"Segoe UI";
#elif defined(PLATFORM_ANDROID) || defined(__ANDROID__)
    static constexpr const wchar_t* kFontName = L"Roboto";
#elif defined(PLATFORM_IOS)    || defined(TARGET_OS_IPHONE)
    static constexpr const wchar_t* kFontName = L"SF Pro Text";
#elif defined(PLATFORM_MACOS)  || defined(TARGET_OS_MAC)
    static constexpr const wchar_t* kFontName = L"Helvetica Neue";
#elif defined(PLATFORM_LINUX)  || defined(__linux__)
    static constexpr const wchar_t* kFontName = L"Sans";
#else
    static constexpr const wchar_t* kFontName = L"Arial";
#endif
    static constexpr float kFontSize       = 18.0f;
    static constexpr bool  kBold           = false;
    static constexpr bool  kItalic         = false;
    static constexpr bool  kUnderline      = false;
    static constexpr bool  kStrikethrough  = false;
}

struct VKTextFadeData {
    std::wstring      text;
    TextRenderStyle   fontStyle;
    XMFLOAT4          startColor    = { 0.0f, 0.0f, 0.0f, 0.0f };
    XMFLOAT4          endColor      = { 1.0f, 1.0f, 1.0f, 1.0f };
    XMFLOAT4          fadeOutColor      = { 0.0f, 0.0f, 0.0f, 0.0f };
    XMFLOAT4          fadeOutStartColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float             posX          = 0.0f;
    float             posY          = 0.0f;
    float             fadeInDuration  = 0.5f;
    float             fadeOutDuration = 0.3f;
    float             displayDuration = -1.0f;
    float             pendingDelay    = 0.0f;
    TextFadePhase     phase         = TextFadePhase::FadeIn;
    float             phaseTimer    = 0.0f;
    bool              immediateStop = false;

    VKTextFadeData() {
        fontStyle.fontName      = LoadingTextFX::kFontName;
        fontStyle.fontSize      = LoadingTextFX::kFontSize;
        fontStyle.bold          = LoadingTextFX::kBold;
        fontStyle.italic        = LoadingTextFX::kItalic;
        fontStyle.underline     = LoadingTextFX::kUnderline;
        fontStyle.strikethrough = LoadingTextFX::kStrikethrough;
    }
};

struct VKFXItem {
    int        fxID;
    int        nextEffectID;
    FXType     type;
    FXSubType  subtype;
    int x = 0, y = 0, width = 0, height = 0;
    XMFLOAT4   color = { 1,1,1,1 };
    int        nextEffectIDv = -1;
    bool       restartOnExpire = false;
    int        pixelSize;
    float      duration;
    float      progress;
    float      delay;
    float      timeout;
    XMFLOAT4   targetColor;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastUpdate;

    // Scroll FX
    float depthMultiplier = 1.0f;
    bool  cameraLinked    = false;
    int   scrollSpeed     = 1;
    bool  isPaused        = false;
    BlitObj2DIndexType textureIndex = BlitObj2DIndexType::NONE;
    int currentXOffset = 0, currentYOffset = 0;
    int tileWidth = 0, tileHeight = 0;

    // Explosion
    int originX, originY;
    std::vector<VKParticle> particles;

    // Starfield
    XMFLOAT3 starfieldOrigin = { 0,0,0 };
    bool     starfieldReverse = false;
    std::vector<VKStar> stars;

    // Text scroller
    VKTextScrollData textScrollData;

    // WarpDotTunnel
    VKWarpTunnelData warpTunnelData;

    // TextFadeInOut
    VKTextFadeData textFadeData;
};

struct VKScrollTween {
    BlitObj2DIndexType textureIndex;
    int   from, to;
    float duration, elapsed = 0.0f;
    bool  active = true;
};

struct VKActiveFXState {
    bool  starfieldActive      = false;
    int   starfieldID          = 0;
    bool  tunnelActive         = false;
    int   tunnelID             = 0;
    bool  textScrollerActive   = false;
    std::vector<int> textScrollerIDs;
    bool  fadeEffectActive     = false;
    bool  scrollEffectsActive  = false;
    std::vector<BlitObj2DIndexType> activeScrollTextures;
    VKActiveFXState() { textScrollerIDs.reserve(20); activeScrollTextures.reserve(10); }
};

struct VKCallbackEntry {
    int                    fxID = -1;
    std::function<void()>  callback;
    std::atomic<bool>      isExecuted{ false };
    std::chrono::steady_clock::time_point creationTime;

    VKCallbackEntry() { creationTime = std::chrono::steady_clock::now(); }
    VKCallbackEntry(int id, std::function<void()> cb)
        : fxID(id), callback(std::move(cb)) { creationTime = std::chrono::steady_clock::now(); }
    VKCallbackEntry(const VKCallbackEntry& o)
        : fxID(o.fxID), callback(o.callback), isExecuted(o.isExecuted.load()), creationTime(o.creationTime) {}
    VKCallbackEntry& operator=(const VKCallbackEntry& o) {
        if (this != &o) { fxID = o.fxID; callback = o.callback; isExecuted = o.isExecuted.load(); creationTime = o.creationTime; }
        return *this;
    }
    VKCallbackEntry(VKCallbackEntry&& o) noexcept
        : fxID(o.fxID), callback(std::move(o.callback)), isExecuted(o.isExecuted.load()), creationTime(o.creationTime)
    { o.fxID = -1; o.isExecuted = true; }
    VKCallbackEntry& operator=(VKCallbackEntry&& o) noexcept {
        if (this != &o) { fxID = o.fxID; callback = std::move(o.callback); isExecuted = o.isExecuted.load(); creationTime = o.creationTime; o.fxID = -1; o.isExecuted = true; }
        return *this;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// VKFXManager class
// ---------------------------------------------------------------------------------------------------------------
class VKFXManager {
public:
    VKFXManager();
    ~VKFXManager();

    bool bHasCleanedUp = false;

    std::vector<VKFXItem> effects;

    void Initialize();
    void CleanUp();
    void AddEffect(const VKFXItem& fxItem);

    void StopAllFXForResize();
    void RestartFXAfterResize();

    // Primary render entry points (called from VULKAN_RenderFrame.cpp)
    void Render();         // 3D fullscreen effects (fades, starfield via Vulkan pipeline)
    void Render2D();       // 2D overlay effects (particles, text scrollers, tile scrollers)

    // Per-effect Vulkan render — cmd is the active command buffer inside the render pass.
    // viewMatrix is glm::mat4 because VulkanCamera returns GLM types on all platforms.
    void RenderFX(int effectID, VkCommandBuffer cmd, const glm::mat4& viewMatrix);

    // Starfield
    int  starfieldID = 0;
    void CreateStarfield(int numStars, float circularRadius, float resetDepthPos,
                         XMFLOAT3 startPos = { 0,0,0 }, bool reverse = false);
    void StopStarfield();
    void UpdateStarfield(float deltaTime);
    void RenderStarfield(VKFXItem& fxItem, VkCommandBuffer cmd, const glm::mat4& viewMatrix);

    // WarpDotTunnel
    int  tunnelID = 0;
    void StopAllFX();
    void DiscardSavedFXState();  // Clears the scene-transition snapshot without restoring it
    void SaveAndSuspendFXForScene();
    void RestoreFXAfterScene();
    void Init3DWarpDOTTunnel(float x, float y, float z,
                             float minRadius, float maxRadius,
                             TunnelSpinCycle spinCycle,
                             int travelSpeed, bool reverseTravel,
                             int dotsPerCircle, int density);
    void StopWarpDotTunnel();

    // Fader
    bool IsFadeActive() const;
    void FadeToColor(XMFLOAT4 color, float duration, float delay);
    void FadeToBlack(float duration, float delay);
    void FadeToWhite(float duration, float delay);
    void FadeToImage(float duration, float delay);
    void FadeOutThenCallback(XMFLOAT4 color, float duration, float delay, std::function<void()> callback);
    void FadeOutInSequence(XMFLOAT4 fadeOutColor, XMFLOAT4 fadeInColor, float duration, float delay,
                           std::function<void()> midpointCallback);

    // Scroller
    void CancelEffect(int effectID);
    void RestartEffect(int effectID);
    void ChainEffect(int fromEffectID, int toEffectID);
    void StartScrollEffect(BlitObj2DIndexType textureIndex, FXSubType direction, int speed,
                           int tileWidth, int tileHeight, float delay);
    void StopScrollEffect(BlitObj2DIndexType textureIndex);
    void UpdateScrollSpeed(BlitObj2DIndexType textureIndex, int newSpeed);
    void PauseScroll(BlitObj2DIndexType textureIndex);
    void ResumeScroll(BlitObj2DIndexType textureIndex);
    void SetScrollDirection(BlitObj2DIndexType textureIndex, FXSubType newDirection);
    void FadeScrollSpeed(BlitObj2DIndexType textureIndex, int fromSpeed, int toSpeed, float duration);
    void UpdateTweens(float deltaTime);
    void StartParallaxLayer(BlitObj2DIndexType textureIndex, FXSubType direction,
                            int baseSpeed, float depthMultiplier,
                            int tileWidth, int tileHeight, float delay, bool cameraLinked);

    // Particle explosion
    void CreateParticleExplosion(int startX, int startY, int maxParticles, int maxRadius);
    void RenderParticles(VKFXItem& fxItem);

    // Text scrollers
    void CreateTextScrollerLTOR(const std::wstring& text, const std::wstring& fontName,
        float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float centerHoldTime, float duration,
        float characterSpacing = 0.5f, float wordSpacing = 8.0f);
    void CreateTextScrollerRTOL(const std::wstring& text, const std::wstring& fontName,
        float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float centerHoldTime, float duration,
        float characterSpacing = 0.5f, float wordSpacing = 8.0f);
    void CreateTextScrollerConsistent(const std::wstring& text, const std::wstring& fontName,
        float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float duration,
        float characterSpacing = 0.5f, float wordSpacing = 8.0f);
    void CreateTextScrollerMovie(const std::vector<std::wstring>& textLines, const std::wstring& fontName,
        float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float lineSpacing, float duration,
        float characterSpacing = 0.5f, float wordSpacing = 8.0f);

    float CalculateTextWidthWithSpacing(const std::wstring& text, const std::wstring& fontName,
                                        float fontSize, float characterSpacing, float wordSpacing);
    void StopTextScroller(int effectID);
    void PauseTextScroller(int effectID);
    void ResumeTextScroller(int effectID);
    void UpdateTextScroller(VKFXItem& fxItem, float deltaTime);
    void RenderTextScroller(VKFXItem& fxItem);

    // Loading-screen TextFadeInOut
    int  ShowLoadingText(const std::wstring& text,
                         XMFLOAT4 endColor       = { 1.0f, 1.0f, 1.0f, 1.0f },
                         float fadeInDuration    = 0.5f,
                         float fadeOutDuration   = 0.3f,
                         XMFLOAT4 startColor     = { 0.0f, 0.0f, 0.0f, 0.0f },
                         float posX = -1.0f, float posY = -1.0f,
                         const TextRenderStyle* fontStyle = nullptr);
    void StopLoadingText();
    void RenderLoadingText();
    bool HasActiveLoadingTextEffects() const;

private:
    // WarpDotTunnel private helpers
    void UpdateWarpDotTunnel(VKFXItem& fx, float deltaTime);
    void RenderWarpDotTunnel(VKFXItem& fx, VkCommandBuffer cmd);

    // TextFadeInOut private helpers
    void UpdateTextFadeInOut(VKFXItem& fx, float deltaTime);
    void RenderTextFadeInOut(VKFXItem& fx);

    // Internal helpers
    void ApplyColorFader(VKFXItem& fxItem);
    void ApplyScroller(VKFXItem& fxItem);
    bool CreateFadePipeline();          // Creates Vulkan pipeline for fullscreen fade quad
    void DestroyFadePipeline();
    void RemoveCompletedEffects();
    void RenderFadeFullScreenQuad(VkCommandBuffer cmd, const XMFLOAT4& color);

    float CalculateTextTransparency(float position, float regionStart, float regionEnd, float fadeDistance);
    float CalculateCharacterTransparency(float charPos, float regionStart, float regionEnd, float fadeDistance);
    void  SplitTextIntoLines(const std::wstring& text, std::vector<std::wstring>& lines,
                              float maxWidth, float fontSize);

    std::vector<int> CollectActiveTextScrollerIDs() const {
        std::vector<int> ids; ids.reserve(effects.size());
        for (const auto& fx : effects) if (fx.type == FXType::TextScroller) ids.push_back(fx.fxID);
        return ids;
    }

    bool HasActiveFadeEffects() const {
        for (const auto& fx : effects) if (fx.type == FXType::ColorFader && fx.progress < 1.0f) return true;
        return false;
    }

    void SafelyClearAllEffects() {
        std::vector<VKFXItem>        tmp; tmp.swap(effects);
        std::vector<VKCallbackEntry> tmpc; tmpc.swap(m_pendingCallbacks);
    }

    // Vulkan pipeline for fullscreen fade quad
    VkPipeline       m_fadePipeline       = VK_NULL_HANDLE;
    VkPipelineLayout m_fadePipelineLayout = VK_NULL_HANDLE;

    // Atomic re-entrancy guard
    std::atomic<bool>          m_isRendering{ false };
    std::mutex                 m_effectsMutex;
    std::vector<VKCallbackEntry>   m_pendingCallbacks;
    std::vector<VKScrollTween>     m_activeTweens;
    VKActiveFXState                m_savedFXState;

    // Full-effect snapshot for scene transitions
    std::vector<VKFXItem> m_sceneSavedEffects;
    int                   m_sceneSavedStarfieldID = 0;
    int                   m_sceneSavedTunnelID    = 0;
};

#endif // __USE_VULKAN__
