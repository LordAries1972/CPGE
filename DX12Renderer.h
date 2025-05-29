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
#include "Models.h"
#include "ThreadManager.h"
#include "ConstantBuffer.h"

const std::string RENDERER_NAME_DX12 = "DX12Renderer";

// Reserved Root Parameter Slots for DirectX 12 Render Pipeline
const int DX12_ROOT_PARAM_CONST_BUFFER = 0;                                    // Constant Buffer Root Parameter
const int DX12_ROOT_PARAM_LIGHT_BUFFER = 1;                                    // Model Light Buffer Root Parameter
const int DX12_ROOT_PARAM_DEBUG_BUFFER = 2;                                    // Debug Buffer Root Parameter
const int DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER = 3;                             // Global Light Buffer Root Parameter
const int DX12_ROOT_PARAM_MATERIAL_BUFFER = 4;                                 // Material Buffer Root Parameter
const int DX12_ROOT_PARAM_ENVIRONMENT_BUFFER = 5;                              // Environment Settings Buffer Root Parameter

// Reserved Descriptor Table Slots for DirectX 12 Textures
const int DX12_DESCRIPTOR_DIFFUSE_TEXTURE = 0;                                 // Diffuse Textures
const int DX12_DESCRIPTOR_NORMAL_MAP = 1;                                      // Normal Texture Mappings
const int DX12_DESCRIPTOR_METALLIC_MAP = 2;                                    // Metallic Mappings
const int DX12_DESCRIPTOR_ROUGHNESS_MAP = 3;                                   // Roughness Mappings
const int DX12_DESCRIPTOR_AO_MAP = 4;                                          // Ambient Occlusion Mapping
const int DX12_DESCRIPTOR_ENVIRONMENT_MAP = 5;                                 // Environment Mappings for Reflections

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
    int iOrigWidth = DEFAULT_WINDOW_WIDTH;                                      // Original window width
    int iOrigHeight = DEFAULT_WINDOW_HEIGHT;                                    // Original window height

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

    // Frame contexts for double buffering
    static const UINT FrameCount = 2;                                           // Number of frames in flight
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
    ComPtr<ID3D12Resource> m_constantBuffer{ nullptr };                        // Constant buffer
    ComPtr<ID3D12Resource> m_globalLightBuffer{ nullptr };                     // Global light buffer

    // Texture resources
    ComPtr<ID3D12Resource> m_d3d12Textures[MAX_TEXTURE_BUFFERS_3D];            // 3D texture resources
    ComPtr<ID3D12Resource> m_d2dTextures[MAX_TEXTURE_BUFFERS];                 // 2D texture resources

    // DirectX 11-12 Compatibility Context
    DX11_DX12_CompatibilityContext m_dx11Dx12Compat;                           // Compatibility context

    // Direct2D components (using DirectX 11 on 12 for 2D rendering)
    ComPtr<ID2D1Factory3> m_d2dFactory{ nullptr };                             // Direct2D factory
    ComPtr<ID2D1Device2> m_d2dDevice{ nullptr };                               // Direct2D device
    ComPtr<ID2D1DeviceContext2> m_d2dContext{ nullptr };                       // Direct2D context
    ComPtr<IDWriteFactory> m_dwriteFactory{ nullptr };                         // DirectWrite factory

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
    void Blit2DObject(BlitObj2DIndexType iIndex, int iX, int iY);               // Blit 2D object
    void Blit2DObjectToSize(BlitObj2DIndexType iIndex, int iX, int iY, int iWidth, int iHeight); // Blit 2D object to size
    void Blit2DObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY,
        int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY);            // Blit 2D object at offset
    void Blit2DWrappedObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY,
        int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY);            // Blit wrapped 2D object
    void Clear2DBlitQueue();                                                    // Clear 2D blit queue

    // Window and display management
    void Resize(uint32_t width, uint32_t height) override;                      // Resize renderer
    void ResumeLoader(bool isResizing = false) override;                        // Resume loader thread
    void WaitForGPUToFinish();                                                  // Wait for GPU to finish

    // Video frame rendering
    void DrawVideoFrame(const Vector2& position, const Vector2& size,
        const MyColor& tintColor, ComPtr<ID3D12Resource> texture);              // Draw video frame

    // GUI rendering functions
    void DrawMyTextCentered(const std::wstring& text, const Vector2& position,
        const MyColor& color, const float FontSize,
        float controlWidth, float controlHeight);                               // Draw centered text

    // Display mode functions
    bool SetFullScreen(void) override;                                          // Set fullscreen mode
    bool SetFullExclusive(uint32_t width, uint32_t height) override;            // Set exclusive fullscreen
    bool SetWindowedScreen(void) override;                                      // Set windowed mode

    // Base class overrides for 2D/3D rendering
    void DrawRectangle(const Vector2& position, const Vector2& size,
        const MyColor& color, bool is2D) override;                              // Draw rectangle
    void DrawMyText(const std::wstring& text, const Vector2& position,
        const MyColor& color, const float FontSize) override;                   // Draw text
    void DrawMyText(const std::wstring& text, const Vector2& position,
        const Vector2& size, const MyColor& color,
        const float FontSize) override;                                         // Draw text with size
    void DrawMyTextWithFont(const std::wstring& text, const Vector2& position,
        const MyColor& color, const float FontSize,
        const std::wstring& fontName);                                          // Draw text with custom font
    void DrawTexture(int textureId, const Vector2& position, const Vector2& size,
        const MyColor& tintColor, bool is2D) override;                          // Draw texture
    void RendererName(std::string sThisName) override;                          // Set renderer name

    // Text measurement functions
    float GetCharacterWidth(wchar_t character, float FontSize) override;        // Get character width
    float GetCharacterWidth(wchar_t character, float FontSize,
        const std::wstring& fontName);                                          // Get character width with font
    float CalculateTextWidth(const std::wstring& text, float FontSize,
        float containerWidth) override;                                         // Calculate text width
    float CalculateTextHeight(const std::wstring& text, float FontSize,
        float containerHeight) override;                                        // Calculate text height

    // DirectX 11-12 Compatibility Functions
    bool InitializeDX11On12Compatibility();                                     // Initialize DX11 on DX12 compatibility
    void CleanupDX11On12Compatibility();                                        // Cleanup DX11 on DX12 compatibility
    bool IsDX11CompatibilityAvailable() const;                                  // Check if DX11 compatibility is available
    ComPtr<ID3D11Device> GetDX11CompatDevice() const;                           // Get DX11 compatibility device
    ComPtr<ID3D11DeviceContext> GetDX11CompatContext() const;                   // Get DX11 compatibility context

    // Make render mutex accessible to other components that need D3D12 synchronization
    static std::mutex& GetRenderMutex() { return s_renderMutex; }

    // Mutexes & Atomics for thread safety
    std::mutex globalMutex;                                                     // Global mutex
    static std::mutex s_renderMutex;                                            // Static render mutex
    std::atomic<bool> wasResizing{ false };                                     // Was resizing flag
    std::atomic<bool> D2DBusy{ false };                                         // Direct2D busy flag

private:
    bool bHasCleanedUp = false;                                                 // Has cleanup been performed
    bool m_supportsEffects = true;                                              // Does hardware support effects
    std::string sName;                                                          // Renderer name
    std::chrono::steady_clock::time_point lastTime;                             // Last frame time
    int frameCount = 0;                                                         // Frame counter
    int m_renderTargetWidth = DEFAULT_WINDOW_WIDTH;                             // Render target width
    int m_renderTargetHeight = DEFAULT_WINDOW_HEIGHT;                           // Render target height
    int delay = 0;                                                              // Delay counter
    int loadIndex = 0;                                                          // Load index
    int iPosX = 0;                                                              // Position X
    float fps = 0.0f;                                                           // Frames per second
    uint32_t prevWindowedWidth = 0;                                             // Previous windowed width
    uint32_t prevWindowedHeight = 0;                                            // Previous windowed height

    // Thread Lock Names
    std::string renderFrameLockName = "dx12_renderer_frame_lock";               // Render frame lock name
    std::string D2DLockName = "dx12_d2d_render_lock";                           // Direct2D lock name

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
    void TransitionResource(ID3D12Resource* resource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter);                                      // Transition resource state

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