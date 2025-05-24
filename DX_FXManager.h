#pragma once

//-------------------------------------------------------------------------------------------------
// FXManager.h - Visual FX Queue and Parallax Scroll Effects
//-------------------------------------------------------------------------------------------------
#include "Includes.h"
#include "Renderer.h"
#include "DirectXMath.h"

#include <d3d11.h>
#include <d3dcompiler.h>

// Forward declarations if needed (e.g., Renderer or GUIManager)
class Debug;

using namespace DirectX;

extern std::shared_ptr<Renderer> renderer;

enum class FXType {
    ColorFader,
    PixelOut,
    ScreenWipe,
    Scroller,
    ParticleExplosion,
    Starfield,
    TextScroller,                                                               // NEW: Text scroller effect type
};

enum class FXSubType {
    // Fader Sub-types
    FadeIntoColor,
    FadeToBackground,
    FadeToTargetColor,

    // Screen Wipe Sub-types
    WipeRight,
    WipeLeft,
    WipeUp,
    WipeDown,

    // Scroll Sub-types
    ScrollRight,
    ScrollLeft,
    ScrollUp,
    ScrollDown,
    ScrollUpAndLeft,
    ScrollUpAndRight,
    ScrollDownAndLeft,
    ScrollDownAndRight,

    // NEW: Text Scroller Sub-types
    TXT_SCROLL_LTOR,                                                            // Left to Right text scroller
    TXT_SCROLL_RTOL,                                                            // Right to Left text scroller
    TXT_SCROLL_CONSISTANT,                                                      // Consistent text scroller
    TXT_SCROLL_MOVIE,                                                           // Movie credits style scroller
};

// Structure representing an individual animated particle for explosions
struct Particle
{
    float x, y;
    float r, g, b, a;
    float angle;           // CHANGE: From int to float for proper precision
    float speed;
    float radius;
    float maxRadius;

    int delayBase = 0;     // number of frames to wait
    int delayCount = 0;    // how many frames passed

    bool completed = false;
    bool hasLoggedCompletion = false;
};

// Structure representing an individual star for starfield effect
struct Star
{
    XMFLOAT3 position;   // 3D position of the star
    XMFLOAT4 color;      // Color and alpha (for fading)
    float size;          // Star size
    float speed;         // Movement speed factor
    bool active;         // Whether the star is currently active
};

// NEW: Structure representing text scroll data for text scrollers
struct TextScrollData {
    std::wstring text;                                                          // Text content to scroll
    std::vector<std::wstring> textLines;                                        // Text split into lines for movie scroller
    std::wstring fontName;                                                      // Font name for text rendering
    float fontSize;                                                             // Font size for text rendering
    XMFLOAT4 textColor;                                                         // Base text color (R, G, B, A)
    float scrollSpeed;                                                          // Speed of scrolling movement
    float currentXPosition;                                                     // Current X position for horizontal scrollers
    float currentYPosition;                                                     // Current Y position for vertical scrollers
    float centerHoldTime;                                                       // Time to hold text in center (LTOR/RTOL only)
    float centerHoldTimer;                                                      // Current center hold timer
    float regionWidth;                                                          // Width of scroll region
    float regionHeight;                                                         // Height of scroll region
    float regionX;                                                              // X position of scroll region
    float regionY;                                                              // Y position of scroll region
    int currentLineIndex;                                                       // Current line being displayed (movie scroller)
    float lineSpacing;                                                          // Spacing between lines (movie scroller)
    float characterSpacing;                                                     // Additional spacing between characters
    float wordSpacing;                                                          // Additional spacing between words
    bool isInCenterPhase;                                                       // Whether text is in center hold phase
    bool hasReachedCenter;                                                      // Whether text has reached center position

    // Constructor to initialize default values
    TextScrollData() :
        text(L""), fontName(L"Arial"), fontSize(16.0f), textColor(1.0f, 1.0f, 1.0f, 1.0f),
        scrollSpeed(1.0f), currentXPosition(0.0f), currentYPosition(0.0f),
        centerHoldTime(2.0f), centerHoldTimer(0.0f), regionWidth(800.0f), regionHeight(600.0f),
        regionX(0.0f), regionY(0.0f), currentLineIndex(0), lineSpacing(20.0f),
        characterSpacing(1.0f), wordSpacing(8.0f), isInCenterPhase(false), hasReachedCenter(false) {
    }
};

struct FXItem {
    int fxID; 												        // FX ID number
    int nextEffectID; 										        // Next FX ID number, used for chaining
    FXType type;                                                    // EFX Type
    FXSubType subtype;                                              // EFX Sub-Type
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };                    // Default to white with full alpha
    int nextEffectIDv = -1;                                         // Optional ID to trigger after this FX completes
    bool restartOnExpire = false;                                   // If true, the FX restarts instead of completing
    int pixelSize;                                                  // Used with Pixel/Block 
    float duration;                                                 // Duration of the effect in seconds
    float progress;                                                 // Progress of the effect (0.0 to 1.0)
    float delay;                                                    // Delay before the effect starts
    float timeout;                                                  // Timeout before the effect is removed
    XMFLOAT4 targetColor;                                           // Target color for fading effects
    std::chrono::steady_clock::time_point startTime;                // Start Time
    std::chrono::steady_clock::time_point lastUpdate;               // Last time this was updated.

    // Scroll FX support
    float depthMultiplier = 1.0f;                                   // 1.0 = normal speed, <1.0 = slower, >1.0 = faster
    bool cameraLinked = false;                                      // If true, follow global camera offset
    int scrollSpeed = 1;                                            // Higher is faster
    bool isPaused = false;                                          // If the scroller is paused.
    BlitObj2DIndexType textureIndex = BlitObj2DIndexType::NONE;     // Texture Image ID
    int currentXOffset = 0;                                         // X Offset of Image    
    int currentYOffset = 0;                                         // Y Offset of Image
    int tileWidth = 0;                                              // Image Width
    int tileHeight = 0;                                             // Image Height

    int originX, originY;                                           // Explosion center
    std::vector<Particle> particles;                                // All particles for the effect

    // NEW: Text Scroller support
    TextScrollData textScrollData;                                  // Text scrolling data for text scroller effects
};

struct ScrollTween {
    BlitObj2DIndexType textureIndex;
    int from;
    int to;
    float duration;
    float elapsed = 0.0f;
    bool active = true;
};

struct ParallaxLayerProfile {
    BlitObj2DIndexType textureIndex;
    FXSubType direction;
    int baseSpeed;
    float depthMultiplier;
    int tileWidth;
    int tileHeight;
    float delay;
    bool cameraLinked;
};

// Structure to store active FX state for restoration after resize
struct ActiveFXState {
    bool starfieldActive;                                                       // Whether starfield was active
    int starfieldID;                                                            // Starfield effect ID
    bool textScrollerActive;                                                    // Whether text scroller was active
    std::vector<int> textScrollerIDs;                                           // Active text scroller IDs
    bool fadeEffectActive;                                                      // Whether fade effect was active
    bool scrollEffectsActive;                                                   // Whether scroll effects were active
    std::vector<BlitObj2DIndexType> activeScrollTextures;                       // Textures with active scroll effects
};

// Our FXManager Class
class FXManager {
public:
    FXManager();
    ~FXManager();

    bool bHasCleanedUp = false;
    bool bIsRendering = false;

    void Initialize();
    void CleanUp();
    void AddEffect(const FXItem& fxItem);
    
    // Our Window Resize / RE-Initialize / Restart calls.
    void StopAllFXForResize();
    void RestartFXAfterResize();

    // Our Render Calls
    void Render();
    // For Rendering that requires 3D Device Contexting
    void RenderFX(int effectID, ID3D11DeviceContext* context, const XMMATRIX& worldMatrix);
    // For effects like scroll that use Direct2D (ie. Tiled Img Scroller, Particle Explosion etc)
    void Render2D();                                                            

    // Starfield Utility Calls
    int starfieldID = 0;
    void CreateStarfield(int numStars, float circularRadius, float resetDepthPos);
    void StopStarfield();
    void UpdateStarfield(float deltaTime);
    void RenderStarfield(FXItem& fxItem, ID3D11DeviceContext* context, const XMMATRIX& viewMatrix);

    // The Fader Utility Calls
    bool IsFadeActive() const;
    void FadeToColor(XMFLOAT4 color, float duration, float delay);
    void FadeToBlack(float duration, float delay);
    void FadeToWhite(float duration, float delay);
    void FadeToImage(float duration, float delay);
    void FadeOutThenCallback(XMFLOAT4 color, float duration, float delay, std::function<void()> callback);
    void FadeOutInSequence(XMFLOAT4 fadeOutColor, XMFLOAT4 fadeInColor, float duration, float delay,
        std::function<void()> midpointCallback);

    // Scroller Utility Calls.
    void CancelEffect(int effectID);
    void RestartEffect(int effectID);
    void ChainEffect(int fromEffectID, int toEffectID);
    void StartScrollEffect(BlitObj2DIndexType textureIndex, FXSubType direction, int speed, int tileWidth, int tileHeight, float delay);
    void StopScrollEffect(BlitObj2DIndexType textureIndex);
    void UpdateScrollSpeed(BlitObj2DIndexType textureIndex, int newSpeed);
    void PauseScroll(BlitObj2DIndexType textureIndex);
    void ResumeScroll(BlitObj2DIndexType textureIndex);
    void SetScrollDirection(BlitObj2DIndexType textureIndex, FXSubType newDirection);
    void FadeScrollSpeed(BlitObj2DIndexType textureIndex, int fromSpeed, int toSpeed, float duration);
    void UpdateTweens(float deltaTime);
    void StartParallaxLayer(BlitObj2DIndexType textureIndex, FXSubType direction, int baseSpeed, float depthMultiplier, int tileWidth, int tileHeight, float delay, bool cameraLinked);

    // Particle Explosion calls.
    void CreateParticleExplosion(int startX, int startY, int maxParticles, int maxRadius);
    void RenderParticles(FXItem& fxItem);

    // Text Scroller Utility Calls
    void CreateTextScrollerLTOR(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float centerHoldTime, float duration, float characterSpacing = 0.5f, float wordSpacing = 8.0f);
    void CreateTextScrollerRTOL(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float centerHoldTime, float duration, float characterSpacing = 0.5f, float wordSpacing = 8.0f);
    void CreateTextScrollerConsistent(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float duration, float characterSpacing = 0.5f, float wordSpacing = 8.0f);
    void CreateTextScrollerMovie(const std::vector<std::wstring>& textLines, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
        float regionX, float regionY, float regionWidth, float regionHeight,
        float scrollSpeed, float lineSpacing, float duration, float characterSpacing = 0.5f, float wordSpacing = 8.0f);    
    
    float CalculateTextWidthWithSpacing(const std::wstring& text, const std::wstring& fontName,
        float fontSize, float characterSpacing, float wordSpacing);

    void StopTextScroller(int effectID);
    void PauseTextScroller(int effectID);
    void ResumeTextScroller(int effectID);
    void UpdateTextScroller(FXItem& fxItem, float deltaTime);
    void RenderTextScroller(FXItem& fxItem);

private:
    // Internal Helper functions
    void ApplyColorFader(FXItem& fxItem);
    void ApplyScroller(FXItem& fxItem);
    void LoadFadeShaders();
    void RemoveCompletedEffects();
    void RenderFullScreenQuad(const XMFLOAT4& color);

    // Private text scroller helper functions
    float CalculateTextTransparency(float position, float regionStart, float regionEnd, float fadeDistance);
    float CalculateCharacterTransparency(float charPosition, float regionStart, float regionEnd, float fadeDistance);
    void SplitTextIntoLines(const std::wstring& text, std::vector<std::wstring>& lines, float maxWidth, float fontSize);

    // Render State Handlers
    void RestoreRenderState();
    void SaveRenderState();

    // Store FX state during resize
    ActiveFXState savedFXState;
    
    // Our Effects Mutex
    std::mutex m_effectsMutex;
    
    // Our Vectors
    std::vector<FXItem> effects;
    std::vector<std::pair<FXItem, std::function<void()>>> pendingCallbacks;
    std::vector<ScrollTween> activeTweens;
    std::vector<ParallaxLayerProfile> myIntroSceneLayers;

    // Other required pointers and variables
    ID3D11BlendState* originalBlendState = nullptr;
    ID3D11BlendState* fadeBlendState = nullptr;
    ID3D11RenderTargetView* originalRenderTarget = nullptr;
    ID3D11DepthStencilView* originalDepthStencilView = nullptr;
    ID3D11RasterizerState* originalRasterState = nullptr;
    ID3D11DepthStencilState* originalDepthStencilState = nullptr;
    ID3D11Buffer* fullscreenQuadVertexBuffer = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11Buffer* constantBuffer = nullptr;
    D3D11_VIEWPORT originalViewport = {};

    UINT originalStencilRef = 0;
    UINT numViewports = 0;
};