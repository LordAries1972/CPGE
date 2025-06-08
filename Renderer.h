/* -------------------------------------------------------------------------------
   RENDERER SYSTEM FLOW DIAGRAM
   -------------------------------------------------------------------------------
   Purpose:
       Dynamically selects one rendering backend (DX11, DX12, OpenGL, Vulkan)
       at compile time, but allows engine code to work uniformly via abstraction.

   -------------------------------------------------------------------------------
   COMPILE-TIME DEFINES (from Includes.h):

       #define __USE_DIRECTX_11__
       #define __USE_DIRECTX_12__
       #define __USE_OPENGL__
       #define __USE_VULKAN__

   Only ONE should be defined at a time.

   -------------------------------------------------------------------------------
   INSTANTIATION FLOW (WinMain):

       std::shared_ptr<Renderer> renderer;

   At runtime:

       #if defined(__USE_DIRECTX_11__)
           renderer = std::make_shared<DX11Renderer>();
       #elif defined(__USE_DIRECTX_12__)
           renderer = std::make_shared<DX12Renderer>();
       #elif defined(__USE_OPENGL__)
           renderer = std::make_shared<OpenGLRenderer>();
       #elif defined(__USE_VULKAN__)
           renderer = std::make_shared<VulkanRenderer>();
       #endif

   -------------------------------------------------------------------------------
   CLASS RELATIONSHIP DIAGRAM:

                      (Interface)
                      +---------------------+
                      |      Renderer       |<----------------------------+
                      +---------------------+                             |
                      | + virtual Init()    |                             |
                      | + virtual Draw()    |                             |
                      | + ...               |                             |
                      +---------------------+                             |
                               ^                                          |
                               |                                          |
           +-------------------+-------------------+----------------------+-----------------+
           |                   |                   |                      |                 |
+-------------------+ +------------------+ +--------------------+ +-------------------+ +------------------+
| DX11Renderer      | | DX12Renderer     | | OpenGLRenderer     | | VulkanRenderer    | | FutureRenderer   |
+-------------------+ +------------------+ +--------------------+ +-------------------+ +------------------+
| Overrides Init()  | | Overrides Init() | | Overrides Init()   | | Overrides Init()  | | Overrides Init() |
| Overrides Draw()  | | Overrides Draw() | | Overrides Draw()   | | Overrides Draw()  | | Overrides Draw() |
| Uses D3D11        | | Uses D3D12       | | Uses OpenGL        | | Uses Vulkan       | | Uses CustomAPI   |
+-------------------+ +------------------+ +--------------------+ +-------------------+ +------------------+

   -------------------------------------------------------------------------------
   ENGINE USE CASE:

       When first initialising, the engine does NOT know what renderer type is being used.

       Base Calls that are used:
           renderer->Initialize(...);
           renderer->RenderFrame();         <= Can run independently or as a thread.
           renderer->Resize(w, h);
           renderer->Cleanup();
           renderer->StartRendererThreads();
           renderer->SetFullExclusive(width, height);
           renderer->Cleanup();

       The actual implementation is resolved at runtime via polymorphism.

   -------------------------------------------------------------------------------
   BENEFITS:

       * Clean interface-based design
       * Easily extensible for new backends
       * Keeps engine code renderer-agnostic
       * Compile-time selection = minimal runtime overhead
------------------------------------------------------------------------------- */
#pragma once

#include "Includes.h"
#include "Vectors.h"
#include "Color.h"
#include "Debug.h"
#include <memory>

#include "DXCamera.h"

// Uncomment this line if Renderer is to be a 
// separate tasking thread
#define RENDERER_IS_THREAD

const int MAX_SCREEN_MONITORS = 4;
const bool USE_FPS_DISPLAY = true;

const int MAX_2D_IMG_QUEUE_OBJS = 512;
const LPCWSTR FontName = L"MayaCulpa";

const int MAX_RENDER_OPERATIONS = 4096;

enum class RendererType
{
	RT_NOT_INITIALIZED,
    RT_DirectX11,
	RT_DirectX12,
	RT_OpenGL,
	RT_Vulkan,
};

//------------------------------------------
// Render Object Enums
//------------------------------------------
enum class BlitObj2DType : int {
    OBJTYPE_NONE = 0,
    OBJTYPE_PLAYER,
    OBJTYPE_ENEMY,
    OBJTYPE_ENEMY_BULLET,
    OBJTYPE_FULL_BACKGROUND,
    OBJTYPE_PLAYER_BULLET,
    OBJTYPE_NEUTRAL,
    OBJTYPE_EXPLOSION,
    OBJTYPE_TEXT,
    OBJTYPE_GAMEOBJECT_FLOOR,
    OBJTYPE_GAMEOBJECT_WALL,
    OBJTYPE_GAMEOBJECT_CEILING,
    OBJTYPE_GAMEOBJECT_DOOR,
    OBJTYPE_GAMEOBJECT_KEY,
    OBJTYPE_GAMEOBJECT_HEAL,
    OBJTYPE_GAMEOBJECT_WEAPON,
    OBJTYPE_GAMEOBJECT_WINDOW,
    OBJTYPE_GAMEOBJECT_BOSS,
    OBJTYPE_GAMEOBJECT_BUTTON,
    OBJTYPE_GAMEOBJECT_SWITCH,
    OBJTYPE_GAMEOBJECT_SCROLLBAR,
    OBJTYPE_PROGRESSBAR
};

enum class CanBlitType : int {
    CAN_BLIT_SINGLE = 0,
    CAN_BLIT_MULTI
};

enum class BlitPhaseLevel : int {
    PHASE_LEVEL_1 = 1,
    PHASE_LEVEL_2,
    PHASE_LEVEL_3,
    PHASE_LEVEL_4,
    PHASE_LEVEL_5
};

enum class BlitObj2DIndexType : int {
    NONE = -1,
    BLIT_ALWAYS_CURSOR = 0,
    BG_INTRO = 1,
    BG_LOADER_CIRCLE,
    IMG_WINFRAME1,
    IMG_BUTTONUP1,
    IMG_BTNCLOSEUP1,
    IMG_BEVEL1,
    IMG_TITLEBAR1,
    IMG_TITLEBAR1HL,
    IMG_SCROLLBG1,
    IMG_SCROLLBG2,
    IMG_SCROLLBG3,
    IMG_SPLASH1,
    IMG_GAMEINTRO1,
    IMG_TITLEBAR2,
    IMG_WINBODY2,
    IMG_BUTTON2UP,
    IMG_BUTTON2DOWN,
    IMG_COMPANYLOGO,
    IMG_TAB_RED,
    IMG_TAB_GUNMETALGRAY,
};

struct BlitObj2DDetails
{
    BlitObj2DIndexType iBlitID;				            // The File Index of object to blit.
    int iBlitX;											// The X position to blit the object to.
    int iBlitY;

    int iWidth;											// The Width of the object to blit.
    int iHeight;										// The Height of the object to blit.
    bool bAnimates;										// Does this object animate?
    bool bIsCollidable;									// Is this object collidable?
    int iFrameIndex;									// The current frame index of the object.
};

struct GFXObjQueue
{
    bool bInUse;										// Is this object in use?
    CanBlitType BlitType;				 	            // Can this object be blitted multiple times or just once, and once only, which means in the queue as well?
    BlitPhaseLevel BlitPhase;				            // AT what phase level the object should be rendered .. The Higher the Phase, the later the render to screen.
    BlitObj2DType BlitObjType;				            // What kind of object is this? Player, Game Object?, Text? etc.
    BlitObj2DDetails BlitObjDetails;			        // The details of the object to blit.
};

struct structRenderQueue
{
    bool InUse;
    LONGLONG timestamp;                                 // Time stamp when this item was added to the Render queue.
    bool IsDX2DOperation;                               // If true, this is a 2D Render operation.
    bool IsDX3DOperation;                               // If true, this is a 3D Render operation.
};

struct OSDetails
{
    struct OSPlatform
    {
		bool isWindows = false;                         // Is this Windows?
        bool isLinux = false;                           // Is this Linux?
		bool isMacOS = false;                           // Is this MacOS?
		bool isAndroid = false;                         // Is this Android phone?
		bool isIOS = false;                             // Is this iOS phone?
    };

	OSPlatform Platform;                                // The platform this OS is running on.
    std::wstring OSName;                                // The name of the Operating System.
    std::wstring OSVersion;                             // The version of the Operating System.
    std::wstring OSBuild;                               // The build number of the Operating System.
    std::wstring OSArchitecture;                        // The architecture of the Operating System (e.g., x64, ARM64).
    std::wstring OSManufacturer;                        // The manufacturer of the Operating System.
    std::wstring OSServicePack;                         // The service pack of the Operating System, if any.
};

class Renderer {
/* /--------------------------------------------------------\
 Your public base declarations go here!
 Remember, your function declarations are to be 
 of virtual as this is the base Renderer
 abstract class.  Your derived classes ie (DX11Renderer, 
 DX12Renderer, OpenGLRenderer, VulkanRenderer etc) will
 override these functions as needed.
 \--------------------------------------------------------/
*/

public:
/* 
    / -----------------------------------------------------------------------\
    This is for Win64 Operating systems (Well for me (Daniel. H) anyways, 
    I encourage all contributors and users to move to 64 bit platforms 
    as this is the future!). Daniel H., will NOT support 32bit anymore
    on Windows, Linux, MacOS, iOS & Android OS platforms!
    
    If you are wanting to use 32bit, then you will have to
	do your own fork of the engine and maintain it yourself.
	\-------------------------------------------------------------------------/
*/
    std::atomic<bool> bIsInitialized{ false };
    std::atomic<bool> bIsDestroyed{ false };
    std::atomic<bool> bHasCleanedUp{ false };
    std::atomic<bool> IsWindowMode{ true };
    std::atomic<bool> bIsMinimized{ false };

    // Default toggle flag for displaying models in Wireframe mode.
    // In Runtime, use the F2 key to toggle status.
    bool bWireframeMode = false;
    // These are used when we resize our window
    int iOrigWidth = DEFAULT_WINDOW_WIDTH;
    int iOrigHeight = DEFAULT_WINDOW_HEIGHT;

	// OS Platform Details
	OSDetails osDetails;

	// Our Camera object for 3D rendering
    Camera myCamera;

    // Destructor
    virtual ~Renderer() = default;

    // Required Variables
    RendererType RenderType = RendererType::RT_NOT_INITIALIZED;
    
    // Initialization and Cleanup
    virtual void RendererName(std::string sThisName) = 0;
    virtual void Initialize(HWND hwnd, HINSTANCE hInstance) = 0;
    virtual void Cleanup() = 0;

	// System Testing Functions
	//virtual bool TestRendererRequirements() = 0;

    // --------------------------
    // Internal Helper functions.
    // --------------------------
    // Device Access Functions - Returns generic pointers castable to ComPtr
    // These functions return void* pointers that can be cast to the appropriate
    // ComPtr types in derived renderer implementations
    virtual void* GetDevice() = 0;                                          // Returns generic device pointer (castable to ComPtr<ID3D11Device>, etc.)
    virtual void* GetDeviceContext() = 0;                                   // Returns generic device context pointer (castable to ComPtr<ID3D11DeviceContext>, etc.)
    virtual void* GetSwapChain() = 0;                                       // Returns generic swap chain pointer (castable to ComPtr<IDXGISwapChain>, etc.)

    // INTERNAL THREAD: Add the virtual declaration for RenderFrame
    virtual void RenderFrame() = 0;
    // INTERNAL THREAD: Add the virtual declaration for LoaderTaskThread
    virtual void LoaderTaskThread() = 0;
	// Thread Helper Functions
    virtual bool StartRendererThreads() = 0;
    virtual void ResumeLoader(bool isResizing = false) = 0;

	// Window / Screen Management
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual bool SetFullScreen(void) = 0;
    virtual bool SetFullExclusive(uint32_t width, uint32_t height) = 0;
    virtual bool SetWindowedScreen(void) = 0;

	// Primitive Drawing Functions
    virtual void DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D) = 0;
    virtual void DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize) = 0;
    virtual void DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize) = 0;
    virtual void DrawTexture(int textureId, const Vector2& position, const Vector2& size, const MyColor& tintColor, bool is2D) = 0;
    virtual void DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, float controlWidth, float controlHeight) = 0;
    virtual void DrawMyTextWithFont(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, const std::wstring& fontName) = 0;

    virtual float GetCharacterWidth(wchar_t character, float FontSize) = 0;
    virtual float GetCharacterWidth(wchar_t character, float FontSize, const std::wstring& fontName) = 0;
    virtual float CalculateTextWidth(const std::wstring& text, float FontSize, float containerWidth) = 0;
    virtual float CalculateTextHeight(const std::wstring& text, float FontSize, float containerHeight) = 0;

    #if defined(_WIN64) || defined(_WIN32)
        // Blitting Functions
        virtual void Blit2DWrappedObjectAtOffset(BlitObj2DIndexType iIndex, int iBlitX, int iBlitY, int iXOffset, int iYOffset, int iTileSizeX, int iTileSizeY) = 0;
        // Draws a single X x Y sized pixel at the specified position with the given RGBA color.
        virtual void Blit2DColoredPixel(int x, int y, float pixelSize, XMFLOAT4 color) = 0;
    #endif

// Your private base declarations go here!
private:


// Your protected base declarations go here!
protected:
    LPCWSTR sName = L"default_";                                                        // Base default Renderer Name
    Renderer() = default;                                                               // Protected constructor to prevent instantiation
};

// Do this as this is a singleton abstract virtual base class.
extern std::shared_ptr<Renderer> renderer;

// Other required external references.
extern Debug debug;

int CreateRendererInstance(); // Declaration only
