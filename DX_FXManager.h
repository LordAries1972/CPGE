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

    // NEW Scroll Sub-types
    ScrollRight,
    ScrollLeft,
    ScrollUp,
    ScrollDown,
    ScrollUpAndLeft,
    ScrollUpAndRight,
    ScrollDownAndLeft,
    ScrollDownAndRight,
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
    void Render();
    void RenderFX(int effectID, ID3D11DeviceContext* context, const XMMATRIX& worldMatrix);
    void Render2D();                                                                // For effects like scroll that use Direct2D (ie. Tiled Img Scroller)

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

private:
    std::mutex m_effectsMutex;
    
    std::vector<FXItem> effects;
    std::vector<std::pair<FXItem, std::function<void()>>> pendingCallbacks;
    std::vector<ScrollTween> activeTweens;
    std::vector<ParallaxLayerProfile> myIntroSceneLayers;

    void ApplyColorFader(FXItem& fxItem);
    void ApplyScroller(FXItem& fxItem);
    void LoadFadeShaders();
    void RestoreRenderState();
    void SaveRenderState();
    void RemoveCompletedEffects();
    void RenderFullScreenQuad(const XMFLOAT4& color);

    ID3D11BlendState* originalBlendState = nullptr;
    ID3D11BlendState* fadeBlendState = nullptr;
    ID3D11RenderTargetView* originalRenderTarget = nullptr;
    ID3D11DepthStencilView* originalDepthStencilView = nullptr;
    ID3D11RasterizerState* originalRasterState = nullptr;
    ID3D11DepthStencilState* originalDepthStencilState = nullptr;
    UINT originalStencilRef = 0;

    D3D11_VIEWPORT originalViewport = {};
    UINT numViewports = 0;

    ID3D11Buffer* fullscreenQuadVertexBuffer = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11Buffer* constantBuffer = nullptr;
};
