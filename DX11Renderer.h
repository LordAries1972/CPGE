// -------------------------------------------------------------------------------------------------------------
// Notes Last Modified: 07-03-2025
//
// This is the implementation of the DX11Renderer class, which is a concrete implementation of the Renderer.
// It uses DirectX 11 for rendering 3D graphics and Direct2D/DirectWrite for 2D graphics and text rendering.
// The class is responsible for initializing the rendering pipeline, loading shaders, and rendering 3D/2D objects.
// 
// The DX11Renderer class is designed to be used in a multithreaded environment, where the rendering is done.
// in a separate thread to avoid blocking the main thread. The class provides methods for rendering 2D and 3D
// objects, as well as text rendering and texture loading.
// 
// PLEASE NOTE:
// ============
// This implementation is to maintain support for the older video cards and OS systems.  For newer systems,
// Please refer to the DX12Renderer or respective class implementations such as VulKAN and OpenGL2/3.
// -------------------------------------------------------------------------------------------------------------
#pragma once

//-------------------------------------------------------------------------------------------------
// DX11Renderer.h - DirectX 11 Renderer Interface
//-------------------------------------------------------------------------------------------------
#include "Includes.h"
#include "Renderer.h"                               // We must include the abstract base class here!!!!

#if defined(__USE_DIRECTX_11__)

#include "DXCamera.h"
#include "Vectors.h"
#include "Color.h"
#include "Models.h"
//#include "SceneManager.h"
#include "ThreadManager.h"
#include "ConstantBuffer.h"

const std::string RENDERER_NAME = "DX11Renderer";

// Reserved Shader Slots for Render Pipeline ( b? slot )
const int SLOT_CONST_BUFFER = 0;                                                // Constant Buffer Slot   
const int SLOT_LIGHT_BUFFER = 1;                                                // Model Light Buffer Slot
const int SLOT_DEBUG_BUFFER = 2;                                                // Debug Buffer Slot
const int SLOT_GLOBAL_LIGHT_BUFFER = 3;                                         // Global Light Buffer Slot
const int SLOT_MATERIAL_BUFFER = 4;                                             // Material Buffer Slot
const int SLOT_ENVIRONMENT_BUFFER = 5;                                          // Environment Settings Buffer Slot

// Reserved Texture Slots for Pixel Shader ( t? slot ).
const int SLOT_diffuseTexture = 0;                                              // Diffuse Textures.
const int SLOT_normalMap = 1;                                                   // Normal Texture Mappings.
const int SLOT_metallicMap = 2;                                                 // Metallic Mappings.
const int SLOT_roughnessMap = 3;                                                // Roughness Mappings.
const int SLOT_aoMap = 4;                                                       // Ambient Occulusion Mapping.
const int SLOT_environmentMap = 5;                                              // Environment Mappings for Reflections.

// Reserved Sampler Slots for Pixel Shader ( s? slot )
const int SLOT_SAMPLER_STATE = 0;
const int SLOT_ENVIRO_SAMPLER_STATE = 1;

// Forward declarations
class Debug;
class SystemUtils;
class GUIManager;
class FXManager;
class Camera;
class Model;

using namespace DirectX;

#define DDS_MAGIC 0x20534444  // "DDS "

typedef struct {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    DWORD dwPitchOrLinearSize;
    DWORD dwDepth;                                                              // Only if DDS_HEADER_FLAGS_VOLUME is set in dwFlags.
    DWORD dwMipMapCount;
    DWORD dwReserved1[11];

    struct {
        DWORD dwSize;
        DWORD dwFlags;
        DWORD dwFourCC;
        DWORD dwRGBBitCount;
        DWORD dwRBitMask;
        DWORD dwGBitMask;
        DWORD dwBBitMask;
        DWORD dwABitMask;
    } ddspf; // Pixel format structure

    DWORD dwCaps;
    DWORD dwCaps2;
    DWORD dwCaps3;
    DWORD dwCaps4;
    DWORD dwReserved2;
} DDS_HEADER;

struct AvailModes {										                        // Details of Available Screen Resolution Mode from Enumeration of device.
    bool InUse;
    int iWidth;
    int iHeight;
    int iBPP;
    int iRefreshRate;
    int iMonitor;
    int iNumerator;
    int iDenominator;
    int iScaling;
    int iScanLineOrdering;
};

struct AvailScreenModes {
    int iAdapter = 0;
    std::vector<AvailModes> modes;  // Dynamic storage
}; 

// -------------------------------------------------------------------------------------------------------------
// Our Main DirectX 11 Renderer Class
// -------------------------------------------------------------------------------------------------------------
class DX11Renderer : public Renderer {
public:
    DX11Renderer();
    ~DX11Renderer();

    // These are used when we resize our window
    int iOrigWidth = DEFAULT_WINDOW_WIDTH;
    int iOrigHeight = DEFAULT_WINDOW_HEIGHT;
    
    // Default toggle flag for displaying models in Wireframe mode.
    // In Runtime, use the F2 key to toggle status.
    bool bWireframeMode = false;

    // Instantiate our required classes & structures.
    Camera myCamera;
    GFXObjQueue My2DBlitQueue[MAX_2D_IMG_QUEUE_OBJS];                           // Our 2D Blit Queue
    AvailScreenModes screenModes[MAX_SCREEN_MONITORS];

    // Smart Pointers - ComPtrs
    ComPtr<ID3D11Device> m_d3dDevice{ nullptr };
    ComPtr<IDXGIAdapter1> adapter{ nullptr };
    ComPtr<ID3D11DeviceContext> m_d3dContext{ nullptr };
    ComPtr<ID3D11RenderTargetView> m_renderTargetView{ nullptr };
    ComPtr<IDXGISwapChain1> m_swapChain{ nullptr };                             // Corrected declaration
    ComPtr<ID2D1Bitmap> m_d2dTextures[MAX_TEXTURE_BUFFERS] = {};
    ComPtr<ID3D11ShaderResourceView> m_d3dTextures[MAX_TEXTURE_BUFFERS_3D] = {};
    ComPtr<ID3D11Buffer> m_globalLightBuffer{ nullptr };
	ComPtr<ID3D11Buffer> m_cameraConstantBuffer{ nullptr };
    ComPtr<ID3D11Buffer> m_debugBuffer{ nullptr };

    ComPtr<ID2D1RenderTarget> m_d2dRenderTarget{ nullptr };
    ComPtr<IDXGISurface> dxgiSurface{ nullptr };
    ComPtr<ID2D1Device> m_d2dDevice{ nullptr };                                 // Direct2D Device
    ComPtr<ID2D1DeviceContext> m_d2dContext{ nullptr };                         // Direct2D Device Context
    ComPtr<ID2D1Factory1> m_d2dFactory{ nullptr };                              // Direct2D Factory

    ComPtr<ID3D11DeviceContext> GetImmediateContext() const;

    std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();

    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
        void TestDrawTriangle();                                                // Used to test pipeline functionality only!
    #endif

    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
        void SetDebugMode(int mode);
    #endif

    // Override all pure virtual functions from the base class Renderer
    void Initialize(HWND hwnd, HINSTANCE hInstance) override;
    bool StartRendererThreads();
    void RenderFrame() override;
    void LoaderTaskThread() override;
    void Cleanup() override;

    // Our function and procedure definitions for this class.
    bool LoadTexture(int textureId, const std::wstring& filename, bool is2D);
    bool LoadAllKnownTextures();
    bool Place2DBlitObjectToQueue(BlitObj2DIndexType iIndex, BlitPhaseLevel BlitPhaseLvl, BlitObj2DType objType, BlitObj2DDetails objDetails, CanBlitType BlitType);
    // Draws a single X x Y sized pixel at the specified position with the given RGBA color.
    void Blit2DColoredPixel(int x, int y, float pixelSize, XMFLOAT4 color);

    void Resize(uint32_t width, uint32_t height) override;
    void WaitForGPUToFinish();
    void UnloadTexture(int textureId, bool is2D);
    void Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY);
    void Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight);
    void Blit2DObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY, int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY);
    void Blit2DWrappedObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY, int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY);
    void Clear2DBlitQueue();
    void ResumeLoader(bool isResizing = false) override;

    // Video Frame Rendering.
    void DrawVideoFrame(const Vector2& position, const Vector2& size, const MyColor& tintColor, ComPtr<ID3D11Texture2D> texture);

    // GuiManager Render functions.
    void DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, float controlWidth, float controlHeight);
    
    // Helper Functions
    bool SetFullScreen(void) override;
    bool SetFullExclusive(uint32_t width, uint32_t height) override;
    bool SetWindowedScreen(void) override;

	// Base class overrides
    void DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D) override;
    void DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize) override;
    void DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize) override;
    void DrawMyTextWithFont(const std::wstring& text, const Vector2& position, const MyColor& color,
        const float FontSize, const std::wstring& fontName);

    void DrawTexture(int textureId, const Vector2& position, const Vector2& size, const MyColor& tintColor, bool is2D) override;
    void RendererName(std::string sThisName) override;

    float GetCharacterWidth(wchar_t character, float FontSize) override;
    float GetCharacterWidth(wchar_t character, float FontSize, const std::wstring& fontName);
    float CalculateTextWidth(const std::wstring& text, float FontSize, float containerWidth) override;
    float CalculateTextHeight(const std::wstring& text, float FontSize, float containerHeight) override;

    // Make render mutex accessible to other components that need D3D11 synchronization
    static std::mutex& GetRenderMutex() { return s_renderMutex; }

    // Mutexes & Atomics for thread safety
    std::mutex globalMutex;
    static std::mutex s_renderMutex;
    std::atomic<bool> wasResizing{ false };
    std::atomic<bool> D2DBusy{ false };

private:
    bool bHasCleanedUp = false;
    bool m_supportsEffects = true;
    std::string sName;
    std::chrono::steady_clock::time_point lastTime;
    int frameCount = 0;
    int m_renderTargetWidth = DEFAULT_WINDOW_WIDTH;
    int m_renderTargetHeight = DEFAULT_WINDOW_HEIGHT;
    int m_renderTargetSampleCount = 0;
    int m_renderTargetSampleQuality = 0;
    int delay = 0;
    int loadIndex = 0;
    int iPosX = 0;
    float fps = 0.0f;
    uint32_t prevWindowedWidth = 0;
    uint32_t prevWindowedHeight = 0;

    D3D_FEATURE_LEVEL m_featureLevel;

    // Thread Lock Names
    std::string renderFrameLockName = "renderer_frame_lock";
    std::string D2DLockName = "d2d_render_lock";

    // Our private function and procedure definitions for this class.
    ComPtr<IDXGIAdapter1> SelectBestAdapter();
    void CreateDeviceAndSwapChain(HWND hwnd);
    void CreateDirect2DResources();
    void CreateRenderTargetViews();
    void CreateDepthStencilBuffer();
    void SetupViewport();
    void SetupPipelineStates();
    void LoadShaders();
    void UpdateConstantBuffers();
    void Clean2DTextures();
    XMFLOAT4 ConvertColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    inline void ThrowError(const std::string& message);

    // Our private ComPtr definitions
    std::atomic<bool> playing{ false };

    ComPtr<ID3DBlob> CompileShader(const std::wstring& filename, const std::string& entryPoint, const std::string& target);
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11Texture2D> m_depthStencilBuffer;

    ComPtr<IDWriteFactory> m_dwriteFactory;

    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11SamplerState> m_samplerState;
    ComPtr<ID3D11BlendState> blendState;

    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;

    ComPtr<ID3D11RasterizerState> m_wireframeState;

    // Mutexes for thread safety
    static std::mutex s_loaderMutex;
};

// We must do this so that our renderers know of our global reference
// and for type casting to the desired interface we intend to use.
extern std::shared_ptr<Renderer> renderer;
// Other main base external references.
extern Debug debug;

extern std::atomic<bool> bResizeInProgress;                                    // Prevents multiple resize operations
extern std::atomic<bool> bFullScreenTransition;                                // Prevents handling during fullscreen transitions

#endif