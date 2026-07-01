#pragma once

//=================================================================================================
// FXManager.h -- Universal Visual FX Manager (all render pipelines)
//
// Single header replacing DX_FXManager.h, DX12FXManager.h, OpenGLFXManager.h,
// VULKAN_FXManager.h.  Conditional compilation selects the active pipeline:
//
//   __USE_DIRECTX_11__  :  DX11 -- COM resources, inline HLSL compiled via D3DCompile
//   __USE_DIRECTX_12__  :  DX12 -- D2D/11on12 overlay (DrawRectangle abstraction)
//   __USE_OPENGL__      :  GL   -- GLSL runtime-compiled fullscreen triangle
//   __USE_VULKAN__      :  VK   -- SPIR-V pipeline via shaderc (optional), push constants
//
// All pipelines share:
//   - Single FXManager class + FXItem struct (no GL/VK prefixed duplicates)
//   - Identical effect logic: fade, scroller, starfield, tunnel, fireworks, zoom, strobe
//   - Pending-effect deferred push (bIsRendering guard) to prevent iterator invalidation
//   - FAST_MATH.FastSinCos() for trig; bitmask wrap for power-of-2 tile sizes
//   - Per-pipeline optimised Render split via bool backgroundOnly
//=================================================================================================

#include "Includes.h"
#include "Renderer.h"

// ---- Platform-specific API headers -------------------------------------------------------

#if defined(__USE_DIRECTX_11__)
    #include <d3d11.h>
    #include <d3dcompiler.h>
    #include "DirectXMath.h"
    using namespace DirectX;

#elif defined(__USE_DIRECTX_12__)
    #include "DirectXMath.h"
    using namespace DirectX;

#elif defined(__USE_OPENGL__)
    #if defined(_WIN32) || defined(_WIN64)
        #ifndef GLEW_STATIC
        #define GLEW_STATIC
        #endif
        #pragma warning(push)
        #pragma warning(disable: 4005)
        #include <GL/glew.h>
        #pragma warning(pop)
    #elif defined(__linux__) || defined(__ANDROID__)
        #include <GL/glew.h>
        #include <glm/glm.hpp>
        #include <glm/gtc/matrix_transform.hpp>
        using XMFLOAT3 = glm::vec3;
        using XMFLOAT4 = glm::vec4;
        using XMMATRIX = glm::mat4;
    #endif

#elif defined(__USE_VULKAN__)
    #include <vulkan/vulkan.h>
    #if defined(PLATFORM_WINDOWS)
        #include <vulkan/vulkan_win32.h>
        #include <DirectXMath.h>
        #include <glm/glm.hpp>
        #include <glm/gtc/matrix_transform.hpp>
        using namespace DirectX;
    #elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
        #include <glm/glm.hpp>
        #include <glm/gtc/matrix_transform.hpp>
        using XMFLOAT3 = glm::vec3;
        using XMFLOAT4 = glm::vec4;
        using XMMATRIX = glm::mat4;
    #endif
#endif

class Debug;
extern std::shared_ptr<Renderer> renderer;

// =========================================================================================
// Enumerations (identical across all pipelines)
// =========================================================================================

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
    ZoomInOut,                                                                  // Pulsing zoom-in / zoom-out on 2D image and/or 3D scene
    Fireworks,                                                                  // Firework rockets that launch, travel, and burst into particles
    ImageFadeStrobe,                                                            // Alpha strobe on a 2D image: fades out to a % then fades back in, looping until stopped
    TileMapScroller,                                                            // 2D tile map renderer/scroller sourced from a tileset atlas image + map-data buffer
    Starfield2D,                                                                // Flat, screen-space 3-layer parallax starfield (see Star2DDirection)
};

// 8 compass-aligned scroll directions for the 2D Starfield (screen-space, not the 3D depth Starfield above)
enum class Star2DDirection {
    EastToWest,                                                                 // Stars travel from the East edge toward the West edge (leftward)
    NorthEastToSouthWest,                                                       // Diagonal: down-left
    NorthToSouth,                                                               // Top of screen down to bottom
    NorthWestToSouthEast,                                                       // Diagonal: down-right
    WestToEast,                                                                 // Stars travel from the West edge toward the East edge (rightward)
    SouthWestToNorthEast,                                                       // Diagonal: up-right
    SouthToNorth,                                                               // Bottom of screen up to top
    SouthEastToNorthWest,                                                       // Diagonal: up-left
};

enum class FXSubType {
    // Fader
    FadeIntoColor,
    FadeToBackground,
    FadeToTargetColor,
    // Screen Wipe
    WipeRight, WipeLeft, WipeUp, WipeDown,
    // Scroller
    ScrollRight, ScrollLeft, ScrollUp, ScrollDown,
    ScrollUpAndLeft, ScrollUpAndRight, ScrollDownAndLeft, ScrollDownAndRight,
    // Text Scroller
    TXT_SCROLL_LTOR,                                                            // Left to Right
    TXT_SCROLL_RTOL,                                                            // Right to Left
    TXT_SCROLL_CONSISTANT,                                                      // Continuous loop
    TXT_SCROLL_MOVIE,                                                           // Movie credits (vertical)
    // TextFadeInOut
    TXT_FADE_IN,                                                                // Fade in from startColor to endColor
    TXT_FADE_OUT,                                                               // Fade out from endColor to fadeOutColor
};

enum class TunnelSpinCycle { None, Clockwise, AntiClockwise, };

enum class ZoomFXFunction {
    Zoom2D,                                                                     // Zoom only the linked 2D blit image
    Zoom3D,                                                                     // Zoom only the 3D scene (FOV/projection)
    ZoomBoth,                                                                   // Zoom both simultaneously
};

enum class TextFadePhase { FadeIn, Holding, FadeOut, Stopped };
enum class StrobePhase   { FadingOut, FadingIn };

// =========================================================================================
// Data structures
// =========================================================================================

struct Particle {
    float x = 0.0f, y = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    float angle    = 0.0f;                                                      // Azimuth or z-depth for starfield
    float speed    = 0.0f;
    float radius   = 0.0f;
    float maxRadius = 0.0f;
    float vx = 0.0f, vy = 0.0f;                                                // Spread direction for reverse-mode starfield
    int   delayBase  = 0;
    int   delayCount = 0;
    bool  completed           = false;
    bool  hasLoggedCompletion = false;
};

struct Star {
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4 color    = { 1.0f, 1.0f, 1.0f, 1.0f };
    float    size     = 1.0f;
    float    speed    = 1.0f;
    bool     active   = true;
};

struct TunnelRing {
    float zPos      = 0.0f;
    float spinAngle = 0.0f;
    float cx        = 0.0f;
    float cy        = 0.0f;
    float bornCx    = 0.0f;
    float bornCy    = 0.0f;
    bool  alive     = true;
    int   colorStep = 0;
};

// Per-pipeline WarpDotTunnel parameters are tuned for the renderer's draw-call cost.
// DX12/DX11 can sustain larger tunnel geometry; GL/VK use a lighter configuration.
struct WarpTunnelData {
    float            startX        = 0.0f;
    float            startY        = 0.0f;
    float            startZ        = 0.0f;
    float            minRadius     = 5.0f;
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    float            maxRadius     = 250.0f;
    int              travelSpeed   = 100;
    int              dotsPerCircle = 32;
    int              density       = 5;
    float totalDistance = 900.0f;
    float nearZ         = 0.0f;
    float farZ          = 900.0f;
    static constexpr float kSideWaveRadius = 80.0f;
    static constexpr float kSideWaveSpeed  = 0.85f;
    static constexpr float kCameraSmooth   = 4.0f;
#else // OpenGL / Vulkan -- lighter config to reduce per-frame 2D draw calls
    float            maxRadius     = 200.0f;
    int              travelSpeed   = 80;
    int              dotsPerCircle = 24;
    int              density       = 4;
    float totalDistance = 800.0f;
    float nearZ         = 0.0f;
    float farZ          = 800.0f;
    static constexpr float kSideWaveRadius = 60.0f;
    static constexpr float kSideWaveSpeed  = 0.5f;
    static constexpr float kCameraSmooth   = 3.0f;
#endif
    TunnelSpinCycle  spinCycle     = TunnelSpinCycle::None;
    bool             reverseTravel = false;
    float spinSpeed       = 0.0f;
    float pathPhaseOffset = 0.0f;
    float sideWaveTime    = 0.0f;
    XMFLOAT3 smoothLookTarget = { 0.0f, 0.0f, 0.0f };
    static constexpr int   kGraySteps   = 8;
    static constexpr float kMaxXYRadius = 300.0f;
    std::vector<TunnelRing> rings;
};

// =========================================================================================
// TileMapData — 2D Tile Map Scroller state
//
// Each cell is a uint32_t: LOWORD = tile index into the tileset atlas (row-major,
// tiles-per-row derived from the atlas bitmap width / tileWidth), HIWORD = flag bits.
// =========================================================================================
namespace TileMapFlags {
    constexpr uint16_t IsCollidable = 0x0001;
    constexpr uint16_t DoesAnimate  = 0x0002;
}

inline uint16_t GetTileIndex(uint32_t cell)      { return static_cast<uint16_t>(cell & 0xFFFFu); }
inline uint16_t GetTileFlags(uint32_t cell)       { return static_cast<uint16_t>((cell >> 16) & 0xFFFFu); }
inline bool     IsTileCollidable(uint32_t cell)   { return (GetTileFlags(cell) & TileMapFlags::IsCollidable) != 0; }
inline bool     TileAnimates(uint32_t cell)        { return (GetTileFlags(cell) & TileMapFlags::DoesAnimate) != 0; }

struct TileMapData {
    BlitObj2DIndexType     atlasIndex = BlitObj2DIndexType::NONE;
    int                    tileWidth  = 0, tileHeight = 0;
    int                    mapWidth   = 0, mapHeight  = 0;                     // Map size in tiles
    std::vector<uint32_t>  mapData;                                            // Flat, row-major: mapData[y*mapWidth+x]
    int                    scrollX    = 0, scrollY    = 0;                     // Top-left world pixel currently displayed, clamped to map bounds
    float                  animTimer  = 0.0f;
    int                    animFrame  = 0;                                     // Added to a tile's index when TileAnimates() is set

    TileMapData() = default;
};

// =========================================================================================
// Star2D / Starfield2DData — 3-Layer flat 2D screen-space starfield state
// =========================================================================================
struct Star2D {
    float x = 0.0f, y = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    int   layer = 0;                                                           // 0 = slowest/dimmest .. 2 = fastest/brightest
};

struct Starfield2DData {
    Star2DDirection      direction        = Star2DDirection::EastToWest;
    float                layerSpeed[3]    = { 20.0f, 45.0f, 90.0f };           // Pixels/sec per layer, layer0 slowest .. layer2 fastest
    int                  layerMaxStars[3] = { 40, 30, 20 };
    std::vector<Star2D>  stars;

    Starfield2DData() = default;
};

struct TextScrollData {
    std::wstring              text;
    std::vector<std::wstring> textLines;
    std::wstring  fontName         = L"Arial";
    float         fontSize         = 16.0f;
    XMFLOAT4      textColor        = { 1.0f, 1.0f, 1.0f, 1.0f };
    float scrollSpeed        = 1.0f;
    float currentXPosition   = 0.0f;
    float currentYPosition   = 0.0f;
    float centerHoldTime     = 2.0f;
    float centerHoldTimer    = 0.0f;
    float regionWidth        = static_cast<float>(config.myConfig.resolutionWidth);
    float regionHeight       = static_cast<float>(config.myConfig.resolutionHeight);
    float regionX            = 0.0f;
    float regionY            = 0.0f;
    int   currentLineIndex   = 0;
    float lineSpacing        = 20.0f;
    float characterSpacing   = 1.0f;
    float wordSpacing        = 8.0f;
    bool  isInCenterPhase    = false;
    bool  hasReachedCenter   = false;
    // Cached metrics -- computed once on first update/render, never recalculated
    bool  widthCached        = false;
    float cachedTextWidth    = 0.0f;
    float cachedTotalTextWidth = 0.0f;
    std::vector<float> cachedCharWidths;
    std::vector<float> cachedCharOffsets;
    std::vector<float> cachedLineWidths;

    TextScrollData() = default;
};

// Default loading-screen font resolved per platform at compile time
namespace LoadingTextFX {
#if   defined(PLATFORM_WINDOWS) || defined(_WIN32) || defined(_WIN64)
    static constexpr const wchar_t* kFontName = L"Segoe UI";
#elif defined(PLATFORM_ANDROID) || defined(__ANDROID__)
    static constexpr const wchar_t* kFontName = L"Roboto";
#elif defined(PLATFORM_IOS) || defined(TARGET_OS_IPHONE)
    static constexpr const wchar_t* kFontName = L"SF Pro Text";
#elif defined(PLATFORM_MACOS) || defined(TARGET_OS_MAC)
    static constexpr const wchar_t* kFontName = L"Helvetica Neue";
#elif defined(PLATFORM_LINUX) || defined(__linux__)
    static constexpr const wchar_t* kFontName = L"Sans";
#else
    static constexpr const wchar_t* kFontName = L"Arial";
#endif
    static constexpr float kFontSize      = 18.0f;
    static constexpr bool  kBold          = false;
    static constexpr bool  kItalic        = false;
    static constexpr bool  kUnderline     = false;
    static constexpr bool  kStrikethrough = false;
}

struct TextFadeData {
    std::wstring    text;
    TextRenderStyle fontStyle;
    XMFLOAT4        startColor        = { 0.0f, 0.0f, 0.0f, 0.0f };
    XMFLOAT4        endColor          = { 1.0f, 1.0f, 1.0f, 1.0f };
    XMFLOAT4        fadeOutColor      = { 0.0f, 0.0f, 0.0f, 0.0f };
    XMFLOAT4        fadeOutStartColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float           posX              = 0.0f;
    float           posY              = 0.0f;
    float           fadeInDuration    = 0.5f;
    float           fadeOutDuration   = 0.3f;
    float           displayDuration   = -1.0f;
    float           pendingDelay      = 0.0f;
    TextFadePhase   phase             = TextFadePhase::FadeIn;
    float           phaseTimer        = 0.0f;
    bool            immediateStop     = false;

    TextFadeData() {
        fontStyle.fontName      = LoadingTextFX::kFontName;
        fontStyle.fontSize      = LoadingTextFX::kFontSize;
        fontStyle.bold          = LoadingTextFX::kBold;
        fontStyle.italic        = LoadingTextFX::kItalic;
        fontStyle.underline     = LoadingTextFX::kUnderline;
        fontStyle.strikethrough = LoadingTextFX::kStrikethrough;
    }
};

struct ZoomData {
    ZoomFXFunction  function         = ZoomFXFunction::Zoom2D;
    float           depth            = 0.25f;
    float           speed            = 1.0f;
    int             link2DImg        = -1;
    float           currentZoomLevel = 0.0f;
    bool            zoomingIn        = true;
    bool            stopRequested    = false;
    int             destX            = 0;
    int             destY            = 0;
    int             destW            = 0;
    int             destH            = 0;
    ZoomData() = default;
};

struct ImageFadeStrobeData {
    BlitObj2DIndexType imageType      = BlitObj2DIndexType::NONE;
    float              fadeOutTarget  = 0.0f;
    float              fadeOverTime   = 1.0f;
    float              currentAlpha   = 1.0f;
    StrobePhase        phase          = StrobePhase::FadingOut;
    float              phaseTimer     = 0.0f;
    bool               stopRequested  = false;
    ImageFadeStrobeData() = default;
};

struct FireworkParticle {
    float x         = 0.0f, y         = 0.0f;
    float angle     = 0.0f;                                                     // Azimuth in radians
    float screenDX  = 0.0f;                                                     // Fibonacci sphere X projection
    float screenDY  = 0.0f;                                                     // Fibonacci sphere Y projection
    float radius    = 0.0f;
    float maxRadius = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float a = 1.0f;
    bool  completed = false;
};

struct FireworkRocket {
    float x      = 0.0f, y      = 0.0f;
    float startX = 0.0f, startY = 0.0f;
    float targetY  = 0.0f;
    float speed    = 4.0f;
    float vx       = 0.0f;                                                      // Horizontal drift for centre-curve steering
    float r = 1.0f, g = 1.0f, b = 1.0f;
    bool  exploded = false;
    bool  done     = false;
    float explodeX = 0.0f, explodeY = 0.0f;
    float expMaxRadius = 50.0f;
    float expR = 1.0f, expG = 1.0f, expB = 1.0f;
    std::vector<FireworkParticle> expParticles;
};

struct FireworksData {
    float freqRate    = 1.0f;
    float launchTimer = 0.0f;
    float baseY       = 0.0f;
    std::vector<FireworkRocket> rockets;
};

// =========================================================================================
// FXItem -- unified per-effect state (single struct for all pipelines)
// =========================================================================================

struct FXItem {
    int fxID         = 0;
    int nextEffectID = -1;
    FXType    type    = FXType::ColorFader;
    FXSubType subtype = FXSubType::FadeIntoColor;
    int x = 0, y = 0, width = 0, height = 0;
    XMFLOAT4  color        = { 1.0f, 1.0f, 1.0f, 1.0f };
    int       nextEffectIDv = -1;
    bool      restartOnExpire = false;
    int       pixelSize  = 1;
    float     duration   = 0.0f;
    float     progress   = 0.0f;
    float     delay      = 0.0f;
    float     timeout    = 0.0f;
    XMFLOAT4  targetColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastUpdate;

    // Scroller
    float              depthMultiplier = 1.0f;
    bool               cameraLinked    = false;
    int                scrollSpeed     = 1;
    bool               isPaused        = false;
    BlitObj2DIndexType textureIndex    = BlitObj2DIndexType::NONE;
    int                currentXOffset  = 0;
    int                currentYOffset  = 0;
    int                tileWidth       = 0;
    int                tileHeight      = 0;

    // Particle explosion
    int originX = 0, originY = 0;
    std::vector<Particle> particles;

    // Starfield (uses particles vector; starfieldOrigin + starfieldReverse control spread)
    XMFLOAT3 starfieldOrigin  = { 0.0f, 0.0f, 0.0f };
    bool     starfieldReverse = false;

    // Subsystem state blocks (only active when type matches)
    TextScrollData       textScrollData;
    WarpTunnelData       warpTunnelData;
    TextFadeData         textFadeData;
    ZoomData             zoomData;
    FireworksData        fireworksData;
    ImageFadeStrobeData  imageFadeStrobeData;
    TileMapData          tileMapData;
    Starfield2DData      starfield2DData;
};

struct ScrollTween {
    BlitObj2DIndexType textureIndex = BlitObj2DIndexType::NONE;
    int   from = 0, to = 0;
    float duration = 0.0f, elapsed = 0.0f;
    bool  active = true;
};

struct ParallaxLayerProfile {
    BlitObj2DIndexType textureIndex;
    FXSubType direction;
    int   baseSpeed;
    float depthMultiplier;
    int   tileWidth, tileHeight;
    float delay;
    bool  cameraLinked;
};

// =========================================================================================
// ActiveFXState -- snapshot taken before resize; restored by loader after resize
// =========================================================================================

struct ActiveFXState {
    bool starfieldActive   = false;
    int  starfieldID       = 0;
    bool tunnelActive      = false;
    int  tunnelID          = 0;
    bool fireworksActive   = false;
    int  fireworksID       = 0;
    bool textScrollerActive = false;
    std::vector<int> textScrollerIDs;
    bool fadeEffectActive   = false;
    bool scrollEffectsActive = false;
    std::vector<BlitObj2DIndexType> activeScrollTextures;
    bool zoomActive         = false;
    int  zoomID             = 0;
    bool imageFadeStrobeActive = false;
    std::vector<BlitObj2DIndexType> activeStrobeImages;
    bool tileMapActive      = false;
    std::vector<int> tileMapIDs;
    bool starfield2DActive  = false;
    std::vector<int> starfield2DIDs;

    ActiveFXState() {
        textScrollerIDs.reserve(20); activeScrollTextures.reserve(10); activeStrobeImages.reserve(10);
        tileMapIDs.reserve(4); starfield2DIDs.reserve(4);
    }

    ActiveFXState(const ActiveFXState& o)
        : starfieldActive(o.starfieldActive), starfieldID(o.starfieldID)
        , tunnelActive(o.tunnelActive), tunnelID(o.tunnelID)
        , fireworksActive(o.fireworksActive), fireworksID(o.fireworksID)
        , textScrollerActive(o.textScrollerActive), textScrollerIDs(o.textScrollerIDs)
        , fadeEffectActive(o.fadeEffectActive), scrollEffectsActive(o.scrollEffectsActive)
        , activeScrollTextures(o.activeScrollTextures)
        , zoomActive(o.zoomActive), zoomID(o.zoomID)
        , imageFadeStrobeActive(o.imageFadeStrobeActive), activeStrobeImages(o.activeStrobeImages)
        , tileMapActive(o.tileMapActive), tileMapIDs(o.tileMapIDs)
        , starfield2DActive(o.starfield2DActive), starfield2DIDs(o.starfield2DIDs) {}

    ActiveFXState& operator=(const ActiveFXState& o) {
        if (this != &o) {
            starfieldActive = o.starfieldActive; starfieldID = o.starfieldID;
            tunnelActive    = o.tunnelActive;    tunnelID    = o.tunnelID;
            fireworksActive = o.fireworksActive; fireworksID = o.fireworksID;
            textScrollerActive = o.textScrollerActive;
            textScrollerIDs = o.textScrollerIDs;
            fadeEffectActive = o.fadeEffectActive; scrollEffectsActive = o.scrollEffectsActive;
            activeScrollTextures = o.activeScrollTextures;
            zoomActive = o.zoomActive; zoomID = o.zoomID;
            imageFadeStrobeActive = o.imageFadeStrobeActive;
            activeStrobeImages = o.activeStrobeImages;
            tileMapActive = o.tileMapActive; tileMapIDs = o.tileMapIDs;
            starfield2DActive = o.starfield2DActive; starfield2DIDs = o.starfield2DIDs;
        }
        return *this;
    }

    ActiveFXState(ActiveFXState&& o) noexcept
        : starfieldActive(o.starfieldActive), starfieldID(o.starfieldID)
        , tunnelActive(o.tunnelActive), tunnelID(o.tunnelID)
        , fireworksActive(o.fireworksActive), fireworksID(o.fireworksID)
        , textScrollerActive(o.textScrollerActive), textScrollerIDs(std::move(o.textScrollerIDs))
        , fadeEffectActive(o.fadeEffectActive), scrollEffectsActive(o.scrollEffectsActive)
        , activeScrollTextures(std::move(o.activeScrollTextures))
        , zoomActive(o.zoomActive), zoomID(o.zoomID)
        , imageFadeStrobeActive(o.imageFadeStrobeActive), activeStrobeImages(std::move(o.activeStrobeImages))
        , tileMapActive(o.tileMapActive), tileMapIDs(std::move(o.tileMapIDs))
        , starfield2DActive(o.starfield2DActive), starfield2DIDs(std::move(o.starfield2DIDs))
    {
        o.starfieldActive = false; o.starfieldID = 0;
        o.tunnelActive    = false; o.tunnelID    = 0;
        o.fireworksActive = false; o.fireworksID = 0;
        o.textScrollerActive = false;
        o.fadeEffectActive = false; o.scrollEffectsActive = false;
        o.zoomActive = false; o.zoomID = 0;
        o.imageFadeStrobeActive = false;
        o.tileMapActive = false;
        o.starfield2DActive = false;
    }

    ActiveFXState& operator=(ActiveFXState&& o) noexcept {
        if (this != &o) {
            starfieldActive = o.starfieldActive; starfieldID = o.starfieldID;
            tunnelActive    = o.tunnelActive;    tunnelID    = o.tunnelID;
            fireworksActive = o.fireworksActive; fireworksID = o.fireworksID;
            textScrollerActive = o.textScrollerActive;
            textScrollerIDs = std::move(o.textScrollerIDs);
            fadeEffectActive = o.fadeEffectActive; scrollEffectsActive = o.scrollEffectsActive;
            activeScrollTextures = std::move(o.activeScrollTextures);
            zoomActive = o.zoomActive; zoomID = o.zoomID;
            imageFadeStrobeActive = o.imageFadeStrobeActive;
            activeStrobeImages = std::move(o.activeStrobeImages);
            tileMapActive = o.tileMapActive; tileMapIDs = std::move(o.tileMapIDs);
            starfield2DActive = o.starfield2DActive; starfield2DIDs = std::move(o.starfield2DIDs);
            o.starfieldActive = false; o.starfieldID = 0;
            o.tunnelActive    = false; o.tunnelID    = 0;
            o.fireworksActive = false; o.fireworksID = 0;
            o.textScrollerActive = false;
            o.fadeEffectActive = false; o.scrollEffectsActive = false;
            o.zoomActive = false; o.zoomID = 0;
            o.imageFadeStrobeActive = false;
            o.tileMapActive = false;
            o.starfield2DActive = false;
        }
        return *this;
    }
};

// =========================================================================================
// CallbackEntry -- FadeOutThenCallback deferred execution with duplicate-execute guard
// =========================================================================================

struct CallbackEntry {
    int                   fxID = -1;
    std::function<void()> callback;
    std::atomic<bool>     isExecuted{ false };
    std::chrono::steady_clock::time_point creationTime;

    CallbackEntry() { creationTime = std::chrono::steady_clock::now(); }
    CallbackEntry(int id, std::function<void()> cb)
        : fxID(id), callback(std::move(cb)) { creationTime = std::chrono::steady_clock::now(); }

    CallbackEntry(const CallbackEntry& o)
        : fxID(o.fxID), callback(o.callback),
          isExecuted(o.isExecuted.load()), creationTime(o.creationTime) {}

    CallbackEntry& operator=(const CallbackEntry& o) {
        if (this != &o) {
            fxID = o.fxID; callback = o.callback;
            isExecuted = o.isExecuted.load(); creationTime = o.creationTime;
        }
        return *this;
    }

    CallbackEntry(CallbackEntry&& o) noexcept
        : fxID(o.fxID), callback(std::move(o.callback)),
          isExecuted(o.isExecuted.load()), creationTime(o.creationTime)
    { o.fxID = -1; o.isExecuted = true; }

    CallbackEntry& operator=(CallbackEntry&& o) noexcept {
        if (this != &o) {
            fxID = o.fxID; callback = std::move(o.callback);
            isExecuted = o.isExecuted.load(); creationTime = o.creationTime;
            o.fxID = -1; o.isExecuted = true;
        }
        return *this;
    }
};

// =========================================================================================
// FXManager class
// =========================================================================================

class FXManager {
public:
    FXManager();
    ~FXManager();

    bool bHasCleanedUp = false;

    std::vector<FXItem> effects;

    void Initialize();
    void CleanUp();
    void AddEffect(const FXItem& fxItem);

    void StopAllFXForResize();
    void RestartFXAfterResize();

    // ---- Render entry points ----
    // backgroundOnly=true  -- pass-1 (before 3D): Starfield, WarpDotTunnel  (DX12 / OpenGL / Vulkan)
    // backgroundOnly=false -- pass-2 (after  3D): ColorFader overlay, callbacks, cleanup
    // DX11 ignores backgroundOnly; starfield/tunnel rendered via RenderFX (see below)
    void Render(bool backgroundOnly = false);

    // Convenience wrapper for the pre-3D pass (calls Render(true)); needed by OpenGL / DX12 call sites
    void RenderBackground();

    // 2D overlay: scrollers, particles, text scrollers, fireworks update, zoom update, strobe update
    void Render2D();

    // ---- Pipeline-specific per-effect render paths ----
#if defined(__USE_DIRECTX_11__)
    // Called from DXRenderFrame.cpp inside the 3D render loop with an active device context
    void RenderFX(int effectID, ID3D11DeviceContext* context, const XMMATRIX& worldMatrix);
    void RenderStarfield(FXItem& fxItem, ID3D11DeviceContext* context, const XMMATRIX& viewMatrix);
#elif defined(__USE_OPENGL__)
    // Called from OpenGL RenderFrame; worldMatrix usually identity for 2D-projection starfield
    void RenderFX(int effectID, const XMMATRIX& worldMatrix);
#elif defined(__USE_VULKAN__)
    // Called from VulkanRenderFrame with an active command buffer
    void RenderFX(int effectID, VkCommandBuffer cmd, const glm::mat4& viewMatrix);
    void RenderStarfield(FXItem& fxItem, VkCommandBuffer cmd, const glm::mat4& viewMatrix);
#endif

    // ---- Starfield ----
    int  starfieldID = 0;
    void CreateStarfield(int numStars, float circularRadius, float resetDepthPos,
                         XMFLOAT3 startPos = { 0.0f, 0.0f, 0.0f }, bool reverse = false);
    void StopStarfield();
    void UpdateStarfield(float deltaTime);

    // ---- WarpDotTunnel ----
    int tunnelID = 0;
    void Init3DWarpDOTTunnel(float x, float y, float z,
                             float minRadius, float maxRadius,
                             TunnelSpinCycle spinCycle,
                             int travelSpeed, bool reverseTravel,
                             int dotsPerCircle, int density);
    void StopWarpDotTunnel();

    // ---- Scene FX lifetime management ----
    void StopAllFX();
    void SaveAndSuspendFXForScene();
    void RestoreFXAfterScene();
    void DiscardSavedFXState();

    // ---- Fader ----
    bool IsFadeActive() const;
    void FadeToColor(XMFLOAT4 color, float duration, float delay);
    void FadeToBlack(float duration, float delay);
    void FadeToWhite(float duration, float delay);
    void FadeToImage(float duration, float delay);
    void FadeOutThenCallback(XMFLOAT4 color, float duration, float delay,
                             std::function<void()> callback);
    void FadeOutInSequence(XMFLOAT4 fadeOutColor, XMFLOAT4 fadeInColor,
                           float duration, float delay,
                           std::function<void()> midpointCallback);

    // ---- Scroller ----
    void CancelEffect(int effectID);
    void RestartEffect(int effectID);
    void ChainEffect(int fromEffectID, int toEffectID);
    void StartScrollEffect(BlitObj2DIndexType textureIndex, FXSubType direction,
                           int speed, int tileWidth, int tileHeight, float delay);
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

    // ---- Particle explosion ----
    void CreateParticleExplosion(int startX, int startY, int maxParticles, int maxRadius);
    void RenderParticles(FXItem& fxItem);

    // ---- Fireworks ----
    int  fireworksID = 0;
    void StartFireworks(float freqRate);
    void StopFireworks();
    void RenderFireworks();

    // ---- ImageFadeStrobe (max 10 simultaneous) ----
    static constexpr int MAX_STROBE_INSTANCES = 10;
    void  StartImageFadeStrobe(BlitObj2DIndexType type, float fadeOutPercentage, float fadeOverTime);
    void  StopImageFadeStrobe(BlitObj2DIndexType type);
    bool  IsImageFadeStrobeActive(BlitObj2DIndexType type) const;
    float GetImageFadeStrobeAlpha(BlitObj2DIndexType type) const;
    void  RenderImageFadeStrobe(BlitObj2DIndexType type, int x, int y, int w, int h);

    // ---- 2D Tile Map Scroller ----
    // mapData may be nullptr if filename is supplied (map is loaded from disk instead);
    // exactly one of the two must be provided. Returns the new effect's fxID, or -1 on failure.
    int  StartTileMapScroller(BlitObj2DIndexType atlasIndex, int tileWidth, int tileHeight,
                              int mapWidth, int mapHeight,
                              const uint32_t* mapData,
                              const std::wstring& filename = L"");
    void StopTileMapScroller(int effectID);
    void ScrollTileMapBy(int effectID, int dx, int dy);                        // Relative move, clamped to map edges
    void SetTileMapPosition(int effectID, int worldX, int worldY);            // Absolute set, clamped to map edges
    uint32_t GetTileMapCell(int effectID, int tileX, int tileY) const;        // Raw cell value for collision/game-logic queries (see TileMapFlags)

    // ---- 3-Layer 2D Starfield (flat, screen-space; distinct from the 3D depth Starfield above) ----
    int  StartStarfield2D(int layer1Count, int layer2Count, int layer3Count,
                         float layer1Speed, float layer2Speed, float layer3Speed,
                         Star2DDirection direction);
    void Set2DStarfieldDirection(int effectID, Star2DDirection direction);
    void StopStarfield2D(int effectID);

    // ---- Text scroller ----
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
    void CreateTextScrollerMovie(const std::vector<std::wstring>& textLines,
        const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float lineSpacing, float duration,
        float characterSpacing = 0.5f, float wordSpacing = 8.0f);
    float CalculateTextWidthWithSpacing(const std::wstring& text, const std::wstring& fontName,
                                        float fontSize, float characterSpacing, float wordSpacing);
    void StopTextScroller(int effectID);
    void PauseTextScroller(int effectID);
    void ResumeTextScroller(int effectID);
    void UpdateTextScroller(FXItem& fxItem, float deltaTime);
    void RenderTextScroller(FXItem& fxItem);

    // ---- ZoomInOut ----
    int  zoomID = -1;
    void ZoomInitialise(ZoomFXFunction function, float depth, float speed,
                        int link2DImg = -1,
                        int destX = 0, int destY = 0, int destW = 0, int destH = 0);
    void StartZoom(float speed);
    void StopZooming();
    bool  IsImageZoomActive(int imgID) const;
    void  RenderZoomedImage(int imgID, int destX, int destY, int destW, int destH);
    float GetCurrent3DZoomFactor() const;

    // ---- TextFadeInOut (loading screen text) ----
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
    // ---- Private render helpers ----
    void RenderStarfield(FXItem& fxItem);                                       // Unified internal path (gets matrices from renderer)
    void RenderWarpDotTunnel(FXItem& fx);                                       // Unified internal path
#if defined(__USE_VULKAN__)
    void RenderWarpDotTunnel(FXItem& fx, VkCommandBuffer cmd);                  // Vulkan external path with explicit cmd
    void RenderFadeFullScreenQuad(VkCommandBuffer cmd, const XMFLOAT4& color);  // Push-constant fade via Vulkan pipeline
#endif

    void UpdateWarpDotTunnel(FXItem& fx, float deltaTime);
    void UpdateTextFadeInOut(FXItem& fx, float deltaTime);
    void RenderTextFadeInOut(FXItem& fx);
    void UpdateZoomInOut(FXItem& fx, float deltaTime);
    void ApplyZoom2D(FXItem& fx);
    void UpdateFireworks(FXItem& fx);
    void DrawFireworksPixels(FXItem& fx);
    void UpdateImageFadeStrobe(FXItem& fx, float deltaTime);
    bool LoadTileMapFromFile(const std::wstring& filename, int mapWidth, int mapHeight, std::vector<uint32_t>& outMapData);
    void RenderTileMapScroller(FXItem& fx);
    void UpdateStarfield2D(FXItem& fx, float deltaTime);
    void RenderStarfield2D(FXItem& fx);
    void RespawnStarfield2DStar(Star2D& star, Star2DDirection direction, int screenW, int screenH, bool initialSpawn);

    void ApplyColorFader(FXItem& fxItem);
    void ApplyScroller(FXItem& fxItem);
    void RemoveCompletedEffects();
    void RenderFullScreenQuad(const XMFLOAT4& color);                          // Dispatches to the active pipeline

    float CalculateTextTransparency(float position, float regionStart, float regionEnd, float fadeDistance);
    float CalculateCharacterTransparency(float charPos, float regionStart, float regionEnd, float fadeDistance);
    void  SplitTextIntoLines(const std::wstring& text, std::vector<std::wstring>& lines,
                              float maxWidth, float fontSize);

    void SaveRenderState();
    void RestoreRenderState();

    // ---- Pipeline-specific init/cleanup helpers ----
    bool LoadFadeShaders();                                                     // DX11: compile inline HLSL; others: no-op

#if defined(__USE_OPENGL__)
    bool CreateFadeShaderProgram();
    void DestroyFadeShaderProgram();
#elif defined(__USE_VULKAN__)
    bool CreateFadePipeline();
    void DestroyFadePipeline();
#endif

    // ---- Private helpers ----
    std::vector<int> CollectActiveTextScrollerIDs() const {
        std::lock_guard<std::recursive_mutex> _fxLock(m_effectsMutex);
        std::vector<int> ids;
        ids.reserve(effects.size());
        for (const auto& fx : effects)
            if (fx.type == FXType::TextScroller)
                ids.push_back(fx.fxID);
        return ids;
    }

    bool HasActiveFadeEffects() const {
        std::lock_guard<std::recursive_mutex> _fxLock(m_effectsMutex);
        for (const auto& fx : effects)
            if (fx.type == FXType::ColorFader && fx.progress < 1.0f)
                return true;
        return false;
    }

    void SafelyClearAllEffects() {
        std::lock_guard<std::recursive_mutex> _fxLock(m_effectsMutex);
        try {
            std::vector<FXItem>        tmp;  tmp.swap(effects);
            std::vector<CallbackEntry> tmpc; tmpc.swap(pendingCallbacks);
        }
        catch (const std::exception& e) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception in SafelyClearAllEffects: " +
                std::wstring(e.what(), e.what() + strlen(e.what())));
        }
    }

    // ---- Thread safety ----
    std::atomic<bool> bIsRendering{ false };
    mutable std::recursive_mutex m_effectsMutex;                            // Recursive so callbacks and cleanup can safely re-enter FX APIs

    // ---- Effect queues ----
    std::vector<CallbackEntry>       pendingCallbacks;
    std::vector<ScrollTween>         activeTweens;
    std::vector<ParallaxLayerProfile> myIntroSceneLayers;
    // Effects added while bIsRendering==true are deferred here and flushed after iteration ends
    std::vector<FXItem>              m_pendingEffects;

    // ---- Resize/scene save state ----
    ActiveFXState    savedFXState;
    std::vector<FXItem> m_sceneSavedEffects;
    int                 m_sceneSavedStarfieldID = 0;
    int                 m_sceneSavedTunnelID    = 0;
    int                 m_sceneSavedFireworksID = 0;
    int                 m_sceneSavedZoomID      = -1;

    // ---- Zoom config (populated by ZoomInitialise, consumed by StartZoom) ----
    ZoomData m_zoomConfig;
    bool     m_hasZoomConfig = false;

    // ---- Pipeline-specific private members ----

#if defined(__USE_DIRECTX_11__)
    // DX11 COM overlay resources
    ID3D11BlendState*        originalBlendState        = nullptr;
    ID3D11BlendState*        fadeBlendState            = nullptr;
    ID3D11RenderTargetView*  originalRenderTarget      = nullptr;
    ID3D11DepthStencilView*  originalDepthStencilView  = nullptr;
    ID3D11RasterizerState*   originalRasterState       = nullptr;
    ID3D11DepthStencilState* originalDepthStencilState = nullptr;
    ID3D11Buffer*            fullscreenQuadVertexBuffer = nullptr;
    ID3D11InputLayout*       inputLayout               = nullptr;
    ID3D11VertexShader*      vertexShader              = nullptr;
    ID3D11PixelShader*       pixelShader               = nullptr;
    ID3D11Buffer*            constantBuffer            = nullptr;
    D3D11_VIEWPORT           originalViewport          = {};
    UINT                     originalStencilRef        = 0;
    UINT                     numViewports              = 0;
#endif

#if defined(__USE_OPENGL__)
    // OpenGL GLSL fade program (fullscreen triangle; no VBO via gl_VertexID)
    GLuint m_fadeShaderProgram = 0;
    GLuint m_fadeVAO           = 0;
    GLint  m_fadeColorUniform  = -1;
#endif

#if defined(__USE_VULKAN__)
    // Vulkan fade pipeline (push constants, dynamic viewport/scissor)
    VkPipeline       m_fadePipeline       = VK_NULL_HANDLE;
    VkPipelineLayout m_fadePipelineLayout = VK_NULL_HANDLE;
    // Weak ref so DestroyFadePipeline is safe if renderer has already been destroyed
    std::weak_ptr<Renderer> m_weakRenderer;
#endif
};
