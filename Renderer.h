/* -------------------------------------------------------------------------------
   RENDERER SYSTEM FLOW DIAGRAM
   -------------------------------------------------------------------------------
   Purpose:
       Dynamically selects one rendering backend (DX11, DX12, OpenGL, Vulkan)
       at compile time, but allows engine code to work uniformly via abstraction.

   -------------------------------------------------------------------------------
   COMPILE-TIME DEFINES (from Constants.h):

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

       The engine does NOT know what renderer is being used.

       It only calls:
           renderer->Initialize(...);
           renderer->RenderFrame();
           renderer->Resize(w, h);
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
#include "Vector2.h"
#include "Color.h"
#include "Debug.h"

#include <memory>

// Windows-specific includes & configuration
#if defined(_WIN32) || defined(_WIN64)
#define __USING_DX_2D__
#define __USING_DX_3D__
#define __USE_DIRECTX_11__
//#define __USE_DIRECTX_12__
#endif

//#define __USE_OPENGL__
//#define __USE_VULKAN__

// Uncomment this line if Renderer is to be a 
// separate tasking thread
//#define RENDERER_IS_THREAD

const int MAX_SCREEN_MONITORS = 4;
const bool USE_FPS_DISPLAY = true;

const int MAX_2D_IMG_QUEUE_OBJS = 512;
const LPCWSTR FontName = L"Arial";

const int MAX_RENDER_OPERATIONS = 4096;

//------------------------------------------------
// File Tables for Assets (AssetsDir is prepended)
//------------------------------------------------

const std::filesystem::path AssetsDir = L"./Assets/";
const std::filesystem::path WinAssetsDir = L".\\Assets\\";

// 2D Textures
inline const std::wstring texFilename[] = {
    L"cursor1.png", L"bg1.jpg", L"loadingring.png", L"window1.png", L"rectbutton1up.png",
    L"winclosebut1up.png", L"bevel1.png", L"titlebar1a.png", L"titlebar1.png",
    L"scrollbg1.png", L"scrollbg2.png", L"scrollbg3.png", L"splash1.png", L"gameintro1.png",
    L"titlebar2.png", L"winbody2.png", L"button2up.png", L"button2down.png", L"logo.png",
    L"tab2red.png", L"tab1gmg.png"
};

// 3D Textures
inline const std::wstring tex3DFilename[] = {
    L"bricks1.png", L"water1.jpg"
};

// 3D Models
inline const std::wstring modelFilePath[] = {
    L"", L"cube1.obj", L"floor1.obj"
};

const int MAX_TEXTURE_BUFFERS = ARRAYSIZE(texFilename);
const int MAX_TEXTURE_BUFFERS_3D = ARRAYSIZE(tex3DFilename);
const int MAX_MODEL_FILES = ARRAYSIZE(modelFilePath);

//------------------------------------------
// VIDEO & Window Specs: Default Settings
//------------------------------------------
const bool START_IN_FULLSCREEN = false;
const int DEFAULT_WINDOW_WIDTH = 800;
const int DEFAULT_WINDOW_HEIGHT = 600;
const float fDEFAULT_WINDOW_WIDTH = 800.0f;
const float fDEFAULT_WINDOW_HEIGHT = 600.0f;

const int MAX_WINDOWS = 32;
const int MAX_SCREEN_MODES = 64;

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

class Renderer {
// Your public base declarations go here!
// Remember, your function declarations are to be 
// of virtual as this is the base Renderer
// abstract class.
public:
    // This is for Win64 Operating systems (Well for me (Daniel. H) anyways, 
    // I encourage all contributors and users to move to 64 bit platforms 
    // as this is the future!). Daniel H., will NOT support 32bit anymore!
    std::atomic<bool> bIsInitialized{ false };
    std::atomic<bool> bIsDestroyed{ false };
    std::atomic<bool> bHasCleanedUp{ false };
    std::atomic<bool> IsWindowMode{ true };
    std::atomic<bool> bIsMinimized{ false };

    // Destructor
    virtual ~Renderer() = default;

    // Required Variables
    RendererType RenderType = RendererType::RT_NOT_INITIALIZED;
    
    // Initialization and Cleanup
    virtual void Initialize(HWND hwnd, HINSTANCE hInstance) = 0;
    // INTERNAL THREAD: Add the virtual declaration for RenderFrame
    virtual void RenderFrame() = 0;
    // INTERNAL THREAD: Add the virtual declaration for LoaderTaskThread
    virtual void LoaderTaskThread() = 0;

    virtual void Cleanup() = 0;
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual bool SetFullScreen(void) = 0;
    virtual bool SetWindowedScreen(void) = 0;
    virtual void DrawRectangle(const Vector2& position, const Vector2& size, const MyColor& color, bool is2D) = 0;
    virtual void DrawMyText(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize) = 0;
    virtual void DrawMyText(const std::wstring& text, const Vector2& position, const Vector2& size, const MyColor& color, const float FontSize) = 0;
    virtual void DrawTexture(int textureId, const Vector2& position, const Vector2& size, const MyColor& tintColor, bool is2D) = 0;
    virtual void DrawMyTextCentered(const std::wstring& text, const Vector2& position, const MyColor& color, const float FontSize, float controlWidth, float controlHeight) = 0;
    virtual float GetCharacterWidth(wchar_t character, float FontSize) = 0;
    virtual float CalculateTextWidth(const std::wstring& text, float FontSize, float containerWidth) = 0;
    virtual float CalculateTextHeight(const std::wstring& text, float FontSize, float containerHeight) = 0;
    virtual void RendererName(std::string sThisName) = 0;

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
