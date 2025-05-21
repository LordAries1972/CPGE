#pragma once

//-------------------------------------------------------------------------------------------------
// GUIManager.h - Graphical UI Management System
//-------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "Vector2.h"
#include "Color.h"
#include "Renderer.h"
#include "SoundManager.h"

// Forward declarations
class DX11Renderer;
class Debug;

using namespace SoundSystem;

const float SNAP_THRESHOLD = 10.0f;                     // Distance to screen edge to trigger snapping

enum class GUIWindowOverlayType {
    Overlay2D,
    Overlay3D,
};

enum class GUIWindowType {
    Standard,
    Alert,
    Dialog,
};

enum class GUIControlType {
    None,
    Button,
    Scrollbar,
    TitleBar,
    TextArea
};

const float SCROLLBAR_WIDTH = 10;
const float BUTTON_WIDTH = 128;
const float GAMEMENU_BUTTON_WIDTH = 250;
const float CLOSEWINBUTTON_SIZE = 16;
const float TITLEBAR_HEIGHT = 28;

struct GUIControl {
    std::string id;
    std::wstring label;
    bool isClickHandled = true;                         // Safety state flag to ensure clicking of mouse is handle correctly.  
    bool isVisible = true;                              // State if the window is visible and should be rendered.
    bool useShadowedText = false;                       // State if we are to use text shadowing.
    Vector2 position;
    Vector2 size;
    float lblFontSize = 8;                              // Label Font Size to use.
    MyColor bgColor;                                    // Background color
    int bgTextureId = -1;                               // Optional background texture ID
    int bgTextureHoverId = -1;                          // Optional background texture ID for when the control is hovered over (used with bgTextureID)

    MyColor shadowedTxtColor = MyColor(0, 0, 0, 255);   // Shadowed Text color (default: black)
    MyColor txtColor = MyColor(255, 255, 255, 255);     // Text color (default: white)
    MyColor hoverColor = MyColor(200, 200, 200, 255);   // Hover color (default: light gray)

    std::function<void()> onMouseBtnDown;
    std::function<void()> onMouseBtnUp;
    std::function<void()> onMouseOver;
    std::function<void()> onMouseMove;                  // Used for TitleBar Control
    std::function<void(int)> onScroll;                  // For scrollbar events
    bool isHovered = false;
    bool isPressed = false;
    GUIControlType type = GUIControlType::None;         // New field to specify control type
};

class GUIManager;                                       // Forward declare

class GUIWindow {
public:
    std::string name;                                   // Windows Assigned Name for Lookup
    GUIWindowType type;                                 // Window Type
    GUIWindowOverlayType overlayType = GUIWindowOverlayType::Overlay2D;   // Overlay Type used for Rendering, 2D or 3D ?
    Vector2 position;                                   // Starting Position for our Window
    Vector2 dragStartPosition;                          // Start Position from when dragging began.
    Vector2 dragStartMousePosition;                     // where the mouse was clicked initially
    Vector2 size;                                       // Size of our Window.    
    MyColor backgroundColor;                            // Background Colour.
    int backgroundTextureId;                            // Optional background texture
    bool isVisible = true;                              // State of Window Visibility
    bool isMinimised = false;                           // State true if the window is minimised.
    bool isDragging = false;                            // true while dragging a window
    bool wasDragging = false;
    bool bWindowDestroy = false;                        // Safety Flag to state when window is closing.
    std::vector<GUIControl> controls;                   // Our Controls list pertaining to this window.
    int scrollPosition = 0;                             // Current scroll position
    int maxScrollPosition = 0;                          // Maximum scroll position
    std::wstring contentText;                           // Text content for the window
    Renderer* myRenderer = nullptr;                     // Renderer reference
    Vector2 contentAreaSize;                            // Size of the content area

    GUIWindow(const std::string& name, GUIWindowType type, const Vector2& position,
        const Vector2& size, const MyColor& backgroundColor, int backgroundTextureId, Renderer* myRenderer);

    void AddControl(const GUIControl& control);
    void Render();
    void UpdateScrollbar(int newPosition);
    void HandleMouseMove(const Vector2& mousePosition, const std::unordered_map<std::string, std::shared_ptr<GUIWindow>>& allWindows);
    void HandleMouseClick(const Vector2& mousePosition, bool& isLeftClick);
    void CalculateScrollbarRange(float FontSize);
    std::wstring WrapText(const std::wstring& text, float maxWidth, float FontSize);
    void MoveWindow(const Vector2& newPosition, const std::unordered_map<std::string, std::shared_ptr<GUIWindow>>& allWindows);
    
    friend class GUIManager;                                                // Allow GUIManager access to internals

private:
    std::mutex mutex;                                                       // Mutex for thread safety
};

class GUIManager {
public:
    GUIManager();
    ~GUIManager();

    void Initialize(Renderer* renderer);                                    // Updated to accept Renderer
    void CreateAlertWindow(const std::wstring& message);                    // No longer needs Renderer parameter
    void CreateGameMenuWindow(const std::wstring& message);
    void CreateMyWindow(const std::string& name, GUIWindowType type,
        const Vector2& position, const Vector2& size,
        const MyColor& backgroundColor,
        int backgroundTextureId);                                           // No longer needs Renderer parameter

    void RemoveWindow(const std::string& name);
    void Render();
    void HandleInput(const std::string& windowName, const Vector2& mousePosition, bool& isLeftClick);
    void HandleAllInput(const Vector2& mousePosition, bool& isLeftClick);
    void SetWindowVisibility(const std::string& name, bool isVisible);
    std::shared_ptr<GUIWindow> GetWindow(const std::string& name);
    std::unordered_map<std::string, std::shared_ptr<GUIWindow>> windows;

private:
    bool bHasCleanedUp = false;
    char buffer[256] = { 0 };

    Renderer* myRenderer = nullptr;                                           // Store the renderer

    std::mutex mutex;                                                         // Mutex for thread safety
};

