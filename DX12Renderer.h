/*
/----------------------------------------------------------------------------------------------------------\
 Notes Last Modified: 27-05-2025

 This is the implementation of the DX12Renderer class, which is a concrete implementation of the Renderer.
 It uses DirectX 12 for rendering 3D graphics and Direct2D/DirectWrite for 2D graphics and text rendering.
 The class is responsible for initializing the rendering pipeline, loading shaders, and rendering 
 3D/2D objects.
 
 The DX12Renderer class is designed to be used in a multithreaded environment, where the rendering is done
 in a separate thread to avoid blocking the main thread. The class provides methods for rendering 2D and 3D
 objects, as well as text rendering and texture loading.
 
 PLEASE NOTE:
 ============
 This implementation is designed for newer hardware supporting DirectX 12. For older systems,
 please use the DX11Renderer implementation instead.

 DirectX 11-12 compatibility calls are included to allow both renderers to work alongside 
 each other if required.
\-----------------------------------------------------------------------------------------------------------/
*/

#pragma once

//-------------------------------------------------------------------------------------------------
// DX12Renderer.h - DirectX 12 Renderer Interface
//-------------------------------------------------------------------------------------------------
#include "Includes.h"
#include "Renderer.h"                               // We must include the abstract base class here!!!!

#if defined(__USE_DIRECTX_12__)

#include "DXCamera.h"
#include "Vectors.h"
#include "Color.h"
#include "DX12Models.h"
#include "ThreadManager.h"
#include "ConstantBuffer.h"
#include "DX12RenderFrame.h"

const std::string RENDERER_NAME_DX12 = "DX12Renderer";

// Reserved Root Parameter Slots for DirectX 12 Render Pipeline
const int DX12_ROOT_PARAM_CONST_BUFFER = 0;                                    // b0: ConstantBuffer  (camera/view/world)
const int DX12_ROOT_PARAM_LIGHT_BUFFER = 1;                                    // b1: LightBuffer     (scene lights)
const int DX12_ROOT_PARAM_DEBUG_BUFFER = 2;                                    // b2: DebugBuffer     (pixel shader debug mode)
const int DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER = 3;                             // b3: GlobalLightBuffer
const int DX12_ROOT_PARAM_MATERIAL_BUFFER = 4;                                 // b4: MaterialBuffer
const int DX12_ROOT_PARAM_ENVIRONMENT_BUFFER = 5;                              // b5: EnvBuffer       (env intensity/tint/fresnel)
const int DX12_ROOT_PARAM_TEXTURE_TABLE = 6;                                   // Descriptor table: t0-t8 SRV textures
const int DX12_ROOT_PARAM_SHADOW_BUFFER = 7;                                   // b6: ShadowBuffer    (PCF shadow map data)

// CBV/SRV/UAV descriptor heap layout for per-model texture SRVs.
// Slots 0-8   : null SRVs (default texture table when a model has no textures; t0-t8 all null)
// Slots 9     : reserved
// Slots 10 .. 10+MAX_TEXTURE_BUFFERS_3D-1 : scene-level 3D texture SRVs (see CreateTextureResources)
// Slots DX12_MODEL_TEXTURE_HEAP_BASE .. +DX12_MODEL_TEXTURE_HEAP_CAPACITY-1 : per-model texture SRVs
// Each loaded model gets 9 consecutive slots (t0=diffuse, t1=normal, t2=metallic, t3=roughness, t4=AO, t5=env, t6=gloss, t7=emissive, t8=shadow).
const UINT DX12_MODEL_TEXTURE_HEAP_BASE     = 10 + MAX_TEXTURE_BUFFERS_3D;  // First slot available for model textures
const UINT DX12_MODEL_TEXTURE_HEAP_CAPACITY = 18432;                         // 2048 models * 9 SRV slots each
// 3 SRV slots immediately after model textures — one per swap-chain frame, for the D2D off-screen composite
const UINT DX12_D2D_COMPOSITE_SRV_BASE      = DX12_MODEL_TEXTURE_HEAP_BASE + DX12_MODEL_TEXTURE_HEAP_CAPACITY;

// Reserved Descriptor Table Slots for DirectX 12 Textures
const int DX12_DESCRIPTOR_DIFFUSE_TEXTURE = 0;                                 // Diffuse Textures
const int DX12_DESCRIPTOR_NORMAL_MAP = 1;                                      // Normal Texture Mappings
const int DX12_DESCRIPTOR_METALLIC_MAP = 2;                                    // Metallic Mappings
const int DX12_DESCRIPTOR_ROUGHNESS_MAP = 3;                                   // Roughness Mappings
const int DX12_DESCRIPTOR_AO_MAP = 4;                                          // Ambient Occlusion Mapping
const int DX12_DESCRIPTOR_ENVIRONMENT_MAP = 5;                                 // Environment Mappings for Reflections
const int DX12_DESCRIPTOR_GLOSS_MAP = 6;                                       // Gloss/smoothness map (roughness = 1-gloss.r)
const int DX12_DESCRIPTOR_EMISSIVE_MAP = 7;                                    // Emissive texture map
const int DX12_DESCRIPTOR_SHADOW_MAP = 8;                                      // Shadow depth map (PCF)

// Reserved Sampler Slots for DirectX 12
const int DX12_SAMPLER_LINEAR = 0;                                             // Linear Sampler
const int DX12_SAMPLER_POINT = 1;                                              // Point Sampler
const int DX12_SAMPLER_ANISOTROPIC = 2;                                        // Anisotropic Sampler

// Forward declarations
class Debug;
class SystemUtils;
class GUIManager;
class FXManager;
class Camera;
class Model;

using namespace DirectX;

#define DDS_MAGIC 0x20534444  // "DDS "

// DirectX 12 specific structures
struct DX12FrameContext {
    ComPtr<ID3D12CommandAllocator> commandAllocator;                            // Command allocator for this frame
    ComPtr<ID3D12CommandAllocator> compositeAllocator;                          // Second allocator for the D2D composite pass
    ComPtr<ID3D12Resource> renderTarget;                                        // Render target for this frame
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;                                      // RTV handle for this frame
    UINT64 fenceValue;                                                          // Fence value for this frame
};

struct DX12DescriptorHeap {
    ComPtr<ID3D12DescriptorHeap> heap;                                          // Descriptor heap
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart;                                       // CPU start handle
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart;                                       // GPU start handle
    UINT handleIncrementSize;                                                   // Handle increment size
    UINT currentOffset;                                                         // Current offset in heap
};

// DirectX 11-12 Compatibility Structure for side-by-side operation
struct DX11_DX12_CompatibilityContext {
    bool bDX11Available;                                                        // Is DirectX 11 available
    bool bDX12Available;                                                        // Is DirectX 12 available
    bool bUsingDX11Fallback;                                                    // Are we using DX11 as fallback
    ComPtr<ID3D11Device> dx11Device;                                            // DirectX 11 device for compatibility
    ComPtr<ID3D11DeviceContext> dx11Context;                                    // DirectX 11 context for compatibility
    ComPtr<ID3D11On12Device> dx11On12Device;                                    // DirectX 11 on 12 device
};

// -------------------------------------------------------------------------------------------------------------
// Our Main DirectX 12 Renderer Class
// -------------------------------------------------------------------------------------------------------------
class DX12Renderer : public Renderer {
public:
    DX12Renderer();
    ~DX12Renderer();

    // Window and display properties
    int iOrigWidth = config.myConfig.resolutionWidth;                           // Original window width
    int iOrigHeight = config.myConfig.resolutionHeight;                         // Original window height

    // Default toggle flag for displaying models in Wireframe mode
    bool bWireframeMode = false;                                                // Wireframe rendering mode toggle

    // Instantiate our required classes & structures
    GFXObjQueue My2DBlitQueue[MAX_2D_IMG_QUEUE_OBJS];                           // Our 2D Blit Queue
    AvailScreenModes screenModes[MAX_SCREEN_MONITORS];                          // Available screen modes

    // DirectX 12 Core Components
    ComPtr<ID3D12Device> m_d3d12Device{ nullptr };                              // DirectX 12 device
    ComPtr<ID3D12CommandQueue> m_commandQueue{ nullptr };                       // Command queue
    ComPtr<IDXGISwapChain4> m_swapChain{ nullptr };                             // Swap chain
    ComPtr<ID3D12GraphicsCommandList> m_commandList{ nullptr };                 // Graphics command list
    ComPtr<ID3D12Fence> m_fence{ nullptr };                                     // Fence for synchronization
    HANDLE m_fenceEvent{ nullptr };                                             // Fence event handle
    UINT64 m_fenceValue{ 0 };                                                   // Current fence value
    UINT m_frameIndex{ 0 };                                                     // Current frame index

    // Frame contexts for triple buffering — reduces Present blocking stalls
    static const UINT FrameCount = 3;                                           // Number of frames in flight
    DX12FrameContext m_frameContexts[FrameCount];                               // Frame contexts

    // Descriptor heaps
    DX12DescriptorHeap m_rtvHeap;                                               // Render Target View heap
    DX12DescriptorHeap m_dsvHeap;                                               // Depth Stencil View heap
    DX12DescriptorHeap m_cbvSrvUavHeap;                                         // CBV/SRV/UAV heap
    DX12DescriptorHeap m_samplerHeap;                                           // Sampler heap

    // Root signature and pipeline state
    ComPtr<ID3D12RootSignature> m_rootSignature{ nullptr };                    // Root signature
    ComPtr<ID3D12PipelineState> m_pipelineState{ nullptr };                    // Pipeline state object

    // Resources
    ComPtr<ID3D12Resource> m_depthStencilBuffer{ nullptr };                    // Depth stencil buffer
    ComPtr<ID3D12Resource> m_constantBuffer{ nullptr };                        // b0: ConstantBuffer
    ComPtr<ID3D12Resource> m_globalLightBuffer{ nullptr };                     // b3: GlobalLightBuffer
    ComPtr<ID3D12Resource> m_envBuffer{ nullptr };                             // b5: EnvBuffer
    ComPtr<ID3D12Resource> m_shadowBuffer{ nullptr };                          // b6: ShadowBuffer (PCF)

    // Texture resources
    ComPtr<ID3D12Resource> m_d3d12Textures[MAX_TEXTURE_BUFFERS_3D];            // 3D texture resources
    ComPtr<ID2D1Bitmap>    m_d2dTextures[MAX_TEXTURE_BUFFERS];                 // 2D texture resources (Direct2D bitmaps)
    ComPtr<ID2D1Bitmap>    m_d2dVideoBitmap;                                   // Dedicated video-frame bitmap (separate from UI texture pool)

    // Per-frame D2D render targets (one per swap chain back buffer)
    ComPtr<ID3D11Resource> m_wrappedBackBuffers[FrameCount];                   // DX12 back buffers wrapped as DX11 resources
    ComPtr<ID2D1Bitmap1>   m_d2dRenderTargets[FrameCount];                     // D2D target bitmaps backed by each back buffer

    // Off-screen D2D compositing (replaces direct back-buffer wrapping, eliminating PRESENT-state transitions)
    ComPtr<ID3D12Resource>      m_d2dOffscreenTex[FrameCount];                 // Per-frame RGBA off-screen textures (DX12)
    ComPtr<ID3D11Resource>      m_d2dWrappedOffscreen[FrameCount];             // DX11 wrappers: InState=RT, OutState=SR
    ComPtr<ID2D1Bitmap1>        m_d2dOffscreenBitmap[FrameCount];              // D2D bitmaps targeting above textures
    D3D12_GPU_DESCRIPTOR_HANDLE m_d2dOffscreenSRV[FrameCount] = {};            // SRV handles in cbvSrvUavHeap

    // Alpha-blend full-screen composite PSO (off-screen D2D → back buffer)
    ComPtr<ID3D12RootSignature> m_compositeRS;
    ComPtr<ID3D12PipelineState> m_compositePSO;

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> m_infoQueue;                                         // Cached info-queue for routing D3D12 validation errors to our log
#endif

    // Null SRV GPU handle: points at the 6 null SRV descriptors in slots 0-5 of
    // m_cbvSrvUavHeap.  Bound as the default texture descriptor table for model
    // draws that do not yet supply per-model DX12 texture SRVs.
    D3D12_GPU_DESCRIPTOR_HANDLE m_nullTextureGPUHandle = {};

    // DirectX 11-12 Compatibility Context
    DX11_DX12_CompatibilityContext m_dx11Dx12Compat;                           // Compatibility context

    // Direct2D components (using DirectX 11 on 12 for 2D rendering)
    ComPtr<ID2D1Factory3> m_d2dFactory{ nullptr };                             // Direct2D factory
    ComPtr<ID2D1Device2> m_d2dDevice{ nullptr };                               // Direct2D device
    ComPtr<ID2D1DeviceContext2> m_d2dContext{ nullptr };                       // Direct2D context
    ComPtr<IDWriteFactory> m_dwriteFactory{ nullptr };                         // DirectWrite factory

    // D2D resource cache — avoids expensive per-call COM creation through 11on12 layer
    ComPtr<ID2D1SolidColorBrush>                                m_generalBrush{ nullptr };
    ComPtr<ID2D1SolidColorBrush>                                m_pixelBrush{ nullptr };    // member (not static local) — reset in Resize()/Cleanup()
    std::unordered_map<std::wstring, ComPtr<IDWriteTextFormat>> m_textFormatCache;

    // Timing
    std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
    void TestDrawTriangle();                                                    // Used to test pipeline functionality only!
#endif

#if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
    void SetDebugMode(int mode);                                                // Set debug mode for pixel shader
#endif

    // Override all pure virtual functions from the base class Renderer
    void Initialize(HWND hwnd, HINSTANCE hInstance) override;                   // Initialize the renderer
    bool StartRendererThreads();                                                // Start renderer threads
    void RenderFrame() override;                                                // Render a frame
    void LoaderTaskThread() override;                                           // Loader task thread
    void Cleanup() override;                                                    // Cleanup resources

    // Texture and resource management functions
    bool LoadTexture(int textureId, const std::wstring& filename, bool is2D);   // Load texture from file
    bool LoadAllKnownTextures();                                                // Load all known textures
    void UnloadTexture(int textureId, bool is2D);                               // Unload texture
    bool Place2DBlitObjectToQueue(BlitObj2DIndexType iIndex, BlitPhaseLevel BlitPhaseLvl,
        BlitObj2DType objType, BlitObj2DDetails objDetails,
        CanBlitType BlitType);                                                  // Place 2D object to blit queue

    // 2D rendering functions
    void Blit2DColoredPixel(int x, int y, float pixelSize, XMFLOAT4 color);     // Draw colored pixel
    void Blit2DColoredPixelFast(int x, int y, float pixelSize, const XMFLOAT4& color); // Hot-path draw for FX code already inside a valid D2D pass
    void Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY);               // Blit 2D object
    void Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight); // Blit 2D object to size
    void Blit2DObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY,
        int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY);            // Blit 2D object at offset
    void Blit2DWrappedObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY,
        int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY);            // Blit wrapped 2D object
    void Blit2DCenteredZoom(BlitObj2DIndexType iIndex, int iDestX, int iDestY,
        int iDestW, int iDestH, float zoomFactor);                              // Blit 2D object with centered zoom crop
    void Blit2DObjectToSizeWithAlpha(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight, float alpha); // Blit with custom opacity
    void Blit2DAtlasTile(BlitObj2DIndexType iIndex, int iTileIndex, int iTileSizeX, int iTileSizeY, int iDestX, int iDestY); // Blit one tile from a tileset atlas
    void Clear2DBlitQueue();                                                    // Clear 2D blit queue

    // Device access (base class interface)
    void* GetDevice() override;                                                 // Returns raw ID3D12Device* pointer
    void* GetDeviceContext() override;                                          // Returns raw ID3D12CommandList* pointer
    void* GetSwapChain() override;                                              // Returns raw IDXGISwapChain4* pointer

    // Thread synchronisation
    void WaitToFinishThenPauseThread() override;                                // Safely drain GPU + pause renderer thread

    // Window and display management
    bool Resize(uint32_t width, uint32_t height) override;                      // Resize renderer
    void ResumeLoader(bool isResizing = false) override;                        // Resume loader thread
    void WaitForGPUToFinish();                                                  // Wait for GPU to finish

    // Video frame rendering
    void DrawVideoFrame(const Vector2& position, const Vector2& size,
        const MyColor& tintColor, ComPtr<ID3D12Resource> texture);              // Draw video frame

    // D2D video texture API — used by MoviePlayer for per-frame video playback
    bool CreateVideoD2DTexture(int& outTextureIndex, UINT width, UINT height);   // Allocate dedicated video bitmap
    bool UpdateVideoD2DTexture(int textureIndex, const BYTE* pData, UINT rowPitch); // Upload one decoded video frame
    void ReleaseVideoD2DTexture(int textureIndex);                               // Free the video bitmap
    void BlitVideoBitmap(float x, float y, float w, float h);                   // Draw m_d2dVideoBitmap (called from MoviePlayer::Render)

    // GUI rendering functions
    void DrawMyTextCentered(const std::wstring& text, const Vector2& position,
        const MyColor& color, const float FontSize,
        float controlWidth, float controlHeight,
        bool bold = false) override;                                            // Draw centered text (bold ignored on DX12 — DWrite synthesis unsafe with this font)

    // 2D clip rect — scissors all D2D drawing to the specified screen rectangle.
    void PushClipRect(float x, float y, float w, float h) override;
    void PopClipRect() override;

    // Display mode functions
    bool SetFullScreen(void) override;                                          // Set fullscreen mode (borderless)
    bool SetFullExclusive(uint32_t width, uint32_t height) override;            // Set exclusive fullscreen
    bool SetWindowedScreen(void) override;                                      // Set windowed mode
    bool SetDisplayMode(DisplayMode mode, int width, int height, int refreshHz) override;  // Unified display-mode entry point
    bool SetDisplayMode() override;                                             // Reads mode/resolution/refresh from config

    // Base class overrides for 2D/3D rendering
    void DrawRectangle(const Vector2& position, const Vector2& size,
        const MyColor& color, bool is2D) override;                              // Draw rectangle
    void DrawCircle(const Vector2& center, float radius, const MyColor& color, bool filled = true) override;
    void DrawMyText(const std::wstring& text, const Vector2& position,
        const MyColor& color, const float FontSize) override;                   // Draw text
    void DrawMyText(const std::wstring& text, const Vector2& position,
        const Vector2& size, const MyColor& color,
        const float FontSize) override;                                         // Draw text with size
    void DrawMyTextWithFont(const std::wstring& text, const Vector2& position,
        const MyColor& color, const float FontSize,
        const std::wstring& fontName) override;                                 // Draw text with custom font
    void DrawMyTextStyled(const std::wstring& text, const Vector2& position,
        const MyColor& color, const TextRenderStyle& style) override;           // Draw styled text (bold/italic/etc)
    void DrawTexture(int textureId, const Vector2& position, const Vector2& size,
        const MyColor& tintColor, bool is2D) override;                          // Draw texture
    void RendererName(std::string sThisName) override;                          // Set renderer name

    // Text measurement functions
    float GetCharacterWidth(wchar_t character, float FontSize) override;        // Get character width
    float GetCharacterWidth(wchar_t character, float FontSize,
        const std::wstring& fontName);                                          // Get character width with font
    float GetCharacterWidth(wchar_t character, float FontSize, bool bold) override; // Get character width (bold-aware)
    float CalculateTextWidth(const std::wstring& text, float FontSize,
        float containerWidth) override;                                         // Calculate text width
    float CalculateTextHeight(const std::wstring& text, float FontSize,
        float containerHeight) override;                                        // Calculate text height

    // DirectX 11-12 Compatibility Functions
    bool InitializeDX11On12Compatibility();                                     // Initialize DX11 on DX12 compatibility
    void CleanupDX11On12Compatibility();                                        // Cleanup DX11 on DX12 compatibility
    bool IsDX11CompatibilityAvailable() const;                                  // Check if DX11 compatibility is available
    bool IsNativeDX12Supported() const;                                         // True when the adapter exposes a full native DX12 3D path
    ComPtr<ID3D11Device> GetDX11CompatDevice() const;                           // Get DX11 compatibility device
    ComPtr<ID3D11DeviceContext> GetDX11CompatContext() const;                   // Get DX11 compatibility context

    // Make render mutex accessible to other components that need D3D12 synchronization
    static std::mutex& GetRenderMutex() { return s_renderMutex; }

    // Mutexes & Atomics for thread safety
    std::mutex globalMutex;                                                     // Global mutex
    static std::mutex s_renderMutex;                                            // Static render mutex
    std::atomic<bool> wasResizing{ false };                                     // Was resizing flag
    std::atomic<bool> D2DBusy{ false };                                         // Direct2D busy flag
    std::atomic<bool> bUseNativeDX12Calls{ false };                             // Adapter passed the native DX12 capability gate for 3D rendering

private:
    // Returns a cached IDWriteTextFormat, creating it on first use.
    // key = fontName + "|" + fontSize + "|" + weight + "|" + style
    IDWriteTextFormat* GetOrCreateTextFormat(const wchar_t* fontName, float fontSize,
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE  style  = DWRITE_FONT_STYLE_NORMAL);

    // Ensures m_generalBrush exists and sets its color.  Returns false if creation fails.
    bool SetGeneralBrushColor(float r, float g, float b, float a);

    bool bHasCleanedUp = false;                                                 // Has cleanup been performed
    bool m_supportsEffects = true;                                              // Does hardware support effects

    // Effective swap-chain buffer count: 3 (triple) or 2 (double), set from
    // config.myConfig.buffering inside CreateDevice().  Array bounds always use
    // the compile-time constant FrameCount = 3; only slots [0..m_effectiveFrameCount-1]
    // are populated with swap-chain back buffers.
    UINT m_effectiveFrameCount = FrameCount;

    // GPU capability flags — populated by CreateDevice(), used for adaptive quality
    UINT64                      m_dedicatedVRAMMB   = 0;                        // Dedicated GPU VRAM in MB
    UINT64                      m_sharedSystemMemMB = 0;                        // Shared system memory in MB (UMA)
    bool                        m_isUMA             = false;                    // True for integrated/UMA architectures
    bool                        m_isLowEndGPU       = false;                    // True when VRAM < 2 GB or UMA
    D3D_FEATURE_LEVEL           m_maxFeatureLevel   = D3D_FEATURE_LEVEL_11_0;  // Highest DX feature level supported
    D3D12_RESOURCE_BINDING_TIER m_resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1; // Descriptor binding tier
    D3D_SHADER_MODEL            m_shaderModel       = D3D_SHADER_MODEL_5_1;     // Highest shader model reported by the D3D12 runtime
    std::string sName;                                                          // Renderer name
    std::chrono::steady_clock::time_point lastTime;                             // Last frame time
    int frameCount = 0;                                                         // Frame counter
    int m_renderTargetWidth  = 0;                                               // Seeded from config.resolutionWidth in Initialize(); updated from swap chain back buffer thereafter
    int m_renderTargetHeight = 0;
    int delay = 0;                                                              // Delay counter
    int loadIndex = 0;                                                          // Load index
    int iPosX = 0;                                                              // Position X
    float fps = 0.0f;                                                           // Frames per second
    uint32_t prevWindowedWidth = 0;                                             // Previous windowed width
    uint32_t prevWindowedHeight = 0;                                            // Previous windowed height
    bool m_isExclusiveFullscreen = false;                                       // True while in DXGI exclusive fullscreen
    bool m_allowTearing = false;                                                // True when DXGI allows Present(0, ALLOW_TEARING) in windowed mode
    HANDLE m_frameLatencyWaitableObject = nullptr;                              // Waitable swap-chain handle; Present() returns immediately when this is in use
    DEVMODE m_originalDesktopMode = {};                                         // Desktop display mode captured before entering exclusive fullscreen

    // Thread Lock Names
    std::string renderFrameLockName = "dx12_renderer_frame_lock";               // Render frame lock name
    std::string D2DLockName = "dx12_d2d_render_lock";                           // Direct2D lock name

    // Timing
    DX12DeltaTimeSmoothing deltaTimeSmoothing;                                  // Per-frame delta smoothing

    // Private DirectX 12 initialization functions
    void CreateDevice();                                                        // Create DirectX 12 device
    void CreateCommandQueue();                                                  // Create command queue
    void CreateSwapChain(HWND hwnd);                                            // Create swap chain
    void CreateDescriptorHeaps();                                               // Create descriptor heaps
    void CreateRenderTargetViews();                                             // Create render target views
    void CreateDepthStencilBuffer();                                            // Create depth stencil buffer
    void CreateRootSignature();                                                 // Create root signature
    void CreatePipelineState();                                                 // Create pipeline state
    void CreateCommandList();                                                   // Create command list
    void CreateFence();                                                         // Create fence
    void LoadShaders();                                                         // Load shaders
    void UpdateConstantBuffers();                                               // Update constant buffers
    void Clean2DTextures();                                                     // Clean 2D textures

    // Helper functions
    XMFLOAT4 ConvertColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);          // Convert color format
    void ThrowError(const std::string& message);                                // Throw error with message
    void WaitForPreviousFrame();                                                // Wait for previous frame
    void MoveToNextFrame();                                                     // Move to next frame
    ComPtr<IDXGIAdapter4> SelectBestAdapter();                                  // Select best graphics adapter
    void CreateDebugLayer();                                                    // Create debug layer
    void LogAdapterInfo(IDXGIAdapter4* adapter);                                // Log adapter information

    // DirectX 12 specific helper functions
    void PopulateCommandList();                                                 // Populate command list
    void ExecuteCommandList();                                                  // Execute command list
    void PresentFrame();                                                        // Present frame
    void ResetCommandList();                                                    // Reset command list
    void CloseCommandList();                                                    // Close command list

    // Resource management helpers
    void CreateConstantBuffers();                                               // Create constant buffers
    void CreateTextureResources();                                              // Create texture resources
    void CreateSamplers();                                                      // Create samplers
    bool UploadTextureData(int textureIndex, const void* textureData,
        size_t dataSize, UINT width, UINT height, DXGI_FORMAT format);         // Upload raw pixel data to GPU texture
    bool GenerateMipmaps(int textureIndex);                                    // Generate mipmaps for a 3D texture
    void OptimizeTextureMemory();                                              // Compact and optimise texture heap usage
    bool PreloadTextures(const std::vector<std::wstring>& textureFilenames);   // Batch-preload a list of texture files
    bool CreateTextureFromMemory(int textureIndex, const void* data,
        size_t dataSize, UINT width, UINT height, DXGI_FORMAT format,
        bool generateMips);                                                    // Create a GPU texture from in-memory pixel data
    bool BatchLoadTextures(const std::vector<std::pair<int, std::wstring>>& textureList, bool is2D); // Batch-load texture pairs (index+path)
    void GetTextureMemoryStats(UINT64& totalMemoryUsed, UINT& texturesLoaded, UINT& availableSlots); // Query texture memory statistics
    bool ValidateTextureResource(int textureIndex, bool is2D);                 // Validate a texture resource is ready for use
    void ReleaseUnusedTextures();                                              // Release textures with no active references
    bool CreateRenderTexture(int textureIndex, UINT width, UINT height,
        DXGI_FORMAT format, bool useAsDepthBuffer);                            // Create a render-target or depth-buffer texture
    void TransitionResource(ID3D12Resource* resource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter);                                      // Transition resource state

    // Game scene render helpers (implemented in DX12RenderFrame.cpp)
    inline void RenderGamePlay(float deltaTime);
    inline void RenderIntroMovie();
    void        RenderBackgroundImage();

    // D2D per-frame target setup — called after init and after each resize
    bool CreateD2DRenderTargets();

    // Off-screen D2D composite pipeline
    bool CreateD2DOffscreenTargets();               // Create off-screen textures + wrapped resources + bitmaps
    bool CreateD2DCompositePSO();                   // Compile + create alpha-blend full-screen-quad PSO
    void CompositeD2DToBackBuffer(                  // Record composite draw onto an already-open command list
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        const D3D12_VIEWPORT&       vp,
        const D3D12_RECT&           scissor);

#ifdef _DEBUG
    void DrainInfoQueue();                          // Drain m_infoQueue and route pending D3D12 validation messages to the game log
#endif

    // Mutexes for thread safety
    static std::mutex s_loaderMutex;                                            // Static loader mutex
};

// Global renderer reference for type casting to desired interface
extern std::shared_ptr<Renderer> renderer;

// External references
extern Debug debug;
extern std::atomic<bool> bResizeInProgress;                                    // Prevents multiple resize operations
extern std::atomic<bool> bFullScreenTransition;                                // Prevents handling during fullscreen transitions

#endif // defined(__USE_DIRECTX_12__)