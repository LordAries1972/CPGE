# GUIManager Class — Complete Usage Guide

## Table of Contents

1. [Overview](#overview)
2. [Important Notes](#important-notes)
3. [Architecture](#architecture)
4. [Getting Started](#getting-started)
5. [Enums and Constants Reference](#enums-and-constants-reference)
6. [GUIManager — Core Methods](#guimanager--core-methods)
7. [GUIWindow — Window Properties](#guiwindow--window-properties)
8. [GUIControl — Control Properties](#guicontrol--control-properties)
9. [Creating Custom Windows](#creating-custom-windows)
10. [Control Types — Detailed Guide](#control-types--detailed-guide)
11. [Input Handling](#input-handling)
12. [Keyboard and Mouse Wheel Routing](#keyboard-and-mouse-wheel-routing)
13. [Window Management](#window-management)
14. [Window Fade System](#window-fade-system)
15. [Custom Rendering Extension](#custom-rendering-extension)
16. [Custom Input Extensions](#custom-input-extensions)
17. [Scrollable Content and Clip Rects](#scrollable-content-and-clip-rects)
18. [Modal Windows](#modal-windows)
19. [Built-in Window Factories](#built-in-window-factories)
20. [Thread Safety](#thread-safety)
21. [Performance Considerations](#performance-considerations)
22. [Common Pitfalls](#common-pitfalls)
23. [Complete Working Examples](#complete-working-examples)

---

## Overview

`GUIManager` is the engine's graphical user interface system. It provides a retained-mode window and control framework that sits on top of every render pipeline. You create named `GUIWindow` objects, populate them with typed `GUIControl` items (buttons, sliders, text areas, title bars, scrollbars, and toggle switches), and the system handles rendering, hit-testing, drag-to-move, input routing, z-ordering, modal blocking, and alpha-transparency fades — all with a single call surface regardless of which renderer is active.

### Key Features

- **Single API across all four renderers** — DX11, DX12, OpenGL, and Vulkan share identical window and control code; renderer-specific drawing is abstracted by the `Renderer` base class
- **Retained-mode** — windows persist until explicitly removed; no per-frame recreation required
- **Named window lookup** — every window has a string name; retrieve it at any time with `GetWindow(name)`
- **Lambda-driven events** — every control callback is a `std::function<void()>` you assign inline; closures are the intended idiom
- **Weak-pointer safety** — lambda captures that outlive the window must use `std::weak_ptr<GUIWindow>` to avoid dangling references
- **Z-order focus model** — the topmost (highest z-order) visible window receives all input exclusively; background windows are locked out
- **Modal blocking** — flag a window `isModal = true` to block all other windows from receiving input
- **Window fades** — non-blocking alpha in/out transitions with optional completion callbacks
- **Custom render extension** — attach `onCustomRender` to a window to draw anything the control set cannot express (scrollbars, circles, custom title bars)
- **Custom input extensions** — attach `onCustomMouseInput`, `onCharInput`, `onBackspace`, `onEnter`, `onMouseWheel` per window
- **Clip rect / scissor** — mark controls `clipContent = true` and set `m_clipPos`/`m_clipSize` on the window for pixel-accurate content area clipping
- **Thread-safe** — a `std::timed_mutex` serialises window creation, removal, and rendering

---

## Important Notes

> **All four render pipelines share this system.**
>
> `GUIManager`, `GUIWindow`, and `GUIControl` are pipeline-agnostic. Every draw call goes through the `Renderer` base class (`DrawRectangle`, `DrawTexture`, `DrawMyText`, `DrawMyTextCentered`, `DrawCircle`, `PushClipRect`, `PopClipRect`). You do not branch on the renderer inside GUI code — the abstraction layer handles it.

> **Font size differences between pipelines.**
>
> OpenGL uses GDI text rendering, which is slightly wider than DWrite (used by DX11/DX12) and FreeType (used by Vulkan). For button labels that must fit a fixed column width use `#if defined(__USE_OPENGL__)` to reduce `lblFontSize` or enable `bold = true` selectively.

> **`MyColor` uses `uint8_t` components (0–255).**
>
> All four channels (`r`, `g`, `b`, `a`) are `uint8_t`. Alpha 0 = fully transparent, 255 = fully opaque.

> **Lambda captures in control callbacks must never capture `GUIControl&` directly.**
>
> Controls are stored in a `std::vector`; any `AddControl` call can reallocate the vector, invalidating all prior references. Capture `std::weak_ptr<GUIWindow>` or copy primitive values instead.

---

## Architecture

```
GUIManager
├── windows : unordered_map<string, shared_ptr<GUIWindow>>
│   ├── GUIWindow "MyDialog"
│   │   ├── controls : vector<GUIControl>
│   │   │   ├── GUIControl (TitleBar)
│   │   │   ├── GUIControl (Button)
│   │   │   └── GUIControl (HSlider)
│   │   ├── m_fade  : WindowFadeState
│   │   ├── onCustomRender : function<void(Renderer*)>
│   │   └── onMouseWheel   : function<void(int)>
│   └── GUIWindow "AlertWindow"
│       └── ...
└── Render() → ticks fades → builds snapshot → renders all visible windows
```

**Input flow per frame:**

```
WndProc / message loop
  └─► GUIManager::HandleAllInput(mousePos, isLeftClick)
         └─► focused window (highest zOrder, visible, non-destroyed)
               ├─► HandleMouseClick  → fires control lambdas
               └─► HandleMouseMove   → updates hover + drag
```

**Render flow per frame:**

```
Renderer::RenderFrame()
  └─► guiManager.Render()
        ├─► (under mutex) tick active window fades
        ├─► (under mutex) build sorted snapshot (visible, non-destroyed)
        ├─► (post mutex) fire completed fade callbacks
        └─► for each window: GUIWindow::Render()
              └─► DrawRectangle / DrawTexture / DrawMyText (× all controls)
```

---

## Getting Started

### Prerequisites

- A running `Renderer` instance (DX11, DX12, OpenGL, or Vulkan)
- `GUIManager.h` included
- `extern GUIManager guiManager;` declared where needed

### Basic Setup

```cpp
// In your application header / main.cpp
#include "GUIManager.h"
GUIManager guiManager;

// After renderer is initialised
void InitGUI(Renderer* renderer) {
    guiManager.Initialize(renderer);
}
```

### Per-Frame Integration

```cpp
// In your render loop (called every frame)
void RenderFrame() {
    // ... 3D scene rendering ...

    // Render all GUI windows on top
    guiManager.Render();
}

// In your input handler (WndProc / message loop)
void HandleInput(Vector2 mousePos, bool isLeftClick) {
    guiManager.HandleAllInput(mousePos, isLeftClick);
}

// Route keyboard events to the focused window
void OnChar(wchar_t c)    { guiManager.HandleChar(c); }
void OnBackspace()        { guiManager.HandleBackspace(); }
void OnEnter()            { guiManager.HandleEnter(); }
void OnMouseWheel(int d)  { guiManager.HandleMouseWheel(d); }
```

---

## Enums and Constants Reference

### `GUIWindowType`

| Value | Description |
|---|---|
| `Standard` | Normal draggable window |
| `Alert` | Alert/notification window |
| `Dialog` | Non-draggable dialog (title bar drag is a no-op) |

### `GUIWindowOverlayType`

| Value | Description |
|---|---|
| `Overlay2D` | Rendered as a 2D UI overlay (default) |
| `Overlay3D` | Reserved for 3D-world-attached windows |

### `GUIWindowFadeType`

| Value | Description |
|---|---|
| `FadeIn` | Alpha 0 → 1 over the specified duration; sets `isVisible = true` immediately |
| `FadeOut` | Alpha 1 → 0 over the specified duration; sets `isVisible = false` on completion |

### `GUIControlType`

| Value | Description |
|---|---|
| `None` | Untyped; ignored by input and render |
| `Button` | Clickable button with hover colour and optional texture |
| `TitleBar` | Drag zone for the window; also renders a labelled header |
| `TextArea` | Static or scrollable text display area |
| `Scrollbar` | Vertical scroll thumb (fires `onScroll`) |
| `HSlider` | Horizontal knob slider with range and `onSliderChanged` callback |
| `ToggleSlider` | ON/OFF toggle knob (green/red) wired through `onSliderChanged` |

### Size Constants

```cpp
const float SNAP_THRESHOLD      = 10.0f;   // Pixels from screen edge / peer window to snap
const float SCROLLBAR_WIDTH     = 10.0f;
const float BUTTON_WIDTH        = 128.0f;
const float GAMEMENU_BUTTON_WIDTH = 250.0f;
const float GAMEMENU_WINDOW_WIDTH = 300.0f;
const float CLOSEWINBUTTON_SIZE = 16.0f;
const float TITLEBAR_HEIGHT     = 28.0f;
```

---

## GUIManager — Core Methods

### `Initialize`

```cpp
void GUIManager::Initialize(Renderer* renderer);
```

Must be called once after the renderer is ready. Stores the renderer pointer used by all subsequently created windows.

```cpp
guiManager.Initialize(myRenderer.get());
```

### `CreateMyWindow`

```cpp
void GUIManager::CreateMyWindow(
    const std::string& name,
    GUIWindowType type,
    const Vector2& position,
    const Vector2& size,
    const MyColor& backgroundColor,
    int backgroundTextureId);
```

Creates a named window. If a window with the same name already exists the call is a no-op (logs an error). The window is immediately visible at the given screen position.

```cpp
guiManager.CreateMyWindow(
    "InventoryWindow",
    GUIWindowType::Standard,
    Vector2(100.0f, 80.0f),     // top-left position in screen pixels
    Vector2(400.0f, 300.0f),    // width × height
    MyColor(20, 20, 40, 220),   // semi-transparent dark background
    int(BlitObj2DIndexType::NONE) // no background texture
);
```

Pass `int(BlitObj2DIndexType::NONE)` for a solid colour background. Pass a loaded texture ID to use a bitmap instead (the solid colour is then ignored).

### `RemoveWindow`

```cpp
void GUIManager::RemoveWindow(const std::string& name);
```

Marks the window for destruction, clears all control callbacks to break lambda capture cycles, and erases it from the `windows` map. Safe to call from a control callback (captures the name by value).

```cpp
okayButton.onMouseBtnDown = [this, windowName = std::string("InventoryWindow")]() {
    RemoveWindow(windowName);   // safe: name is captured by value
};
```

### `GetWindow`

```cpp
std::shared_ptr<GUIWindow> GUIManager::GetWindow(const std::string& name);
```

Returns the window's `shared_ptr` or an empty pointer if not found. Check before use.

```cpp
auto win = guiManager.GetWindow("InventoryWindow");
if (win && !win->bWindowDestroy) {
    win->contentText = L"Updated text";
}
```

### `SetWindowVisibility`

```cpp
void GUIManager::SetWindowVisibility(const std::string& name, bool isVisible);
```

Shows or hides a window instantly (no fade). To fade use `ApplyWindowFade` instead.

### `BringWindowToFront`

```cpp
void GUIManager::BringWindowToFront(const std::string& name);
```

Assigns this window the highest z-order so it receives exclusive input focus. Useful after hiding a window and re-showing it, or when opening a console over other windows.

### `OnWindowResize`

```cpp
void GUIManager::OnWindowResize(int newWidth, int newHeight);
```

Called by the renderer on resolution or fullscreen changes. Repositions the `GameMenuWindow` to the new right edge. If you have other windows that must track screen edges, call this yourself or override `onCustomRender` to recompute positions.

### `Render`

```cpp
void GUIManager::Render();
```

Call once per frame from inside the render loop. Internally: ticks active fades, builds a z-sorted snapshot of visible windows, fires any completed-fade callbacks, then calls `GUIWindow::Render()` for each.

### `HandleAllInput`

```cpp
void GUIManager::HandleAllInput(const Vector2& mousePosition, bool& isLeftClick);
```

Routes mouse input to the topmost focused window. The `isLeftClick` flag is `true` while the left mouse button is held down and `false` when released — it is **not** a single-frame edge trigger. Control callbacks fire on state transitions detected internally.

### `HandleInput` *(single-window variant)*

```cpp
void GUIManager::HandleInput(const std::string& windowName, const Vector2& mousePosition, bool& isLeftClick);
```

Sends input to a specific named window regardless of z-order. Useful for windows that must always receive events (e.g., a minimap overlay that never has focus).

### Keyboard and Wheel

```cpp
void GUIManager::HandleChar(wchar_t c);
void GUIManager::HandleBackspace();
void GUIManager::HandleEnter();
void GUIManager::HandleMouseWheel(int delta);
```

Each method dispatches to the focused window's matching callback (`onCharInput`, `onBackspace`, `onEnter`, `onMouseWheel`). The focused window is the visible non-destroyed window with the highest z-order. No-op if the focused window has no callback registered.

---

## GUIWindow — Window Properties

After calling `GetWindow`, you can read and write these members directly:

| Member | Type | Description |
|---|---|---|
| `name` | `std::string` | Lookup key; set at construction |
| `type` | `GUIWindowType` | Standard / Alert / Dialog |
| `overlayType` | `GUIWindowOverlayType` | 2D (default) or 3D |
| `position` | `Vector2` | Top-left screen position |
| `size` | `Vector2` | Width × height in pixels |
| `backgroundColor` | `MyColor` | RGBA background colour (used when no texture) |
| `backgroundTextureId` | `int` | Loaded texture ID, or `int(BlitObj2DIndexType::NONE)` |
| `isVisible` | `bool` | Whether the window is rendered and receives input |
| `isMinimised` | `bool` | Reserved for minimise behaviour |
| `isDragging` | `bool` | True while user is dragging the title bar |
| `bWindowDestroy` | `bool` | True once `RemoveWindow` has been called; do not write |
| `isModal` | `bool` | When true, all other windows are blocked from input |
| `zOrder` | `int` | Higher = drawn on top + receives input; managed by `BringWindowToFront` |
| `controls` | `vector<GUIControl>` | Ordered list of controls |
| `scrollPosition` | `int` | Current vertical scroll offset |
| `maxScrollPosition` | `int` | Maximum scroll offset |
| `contentText` | `std::wstring` | Optional text body (used by built-in scrolling) |
| `m_hasClip` | `bool` | Enable scissor rect for `clipContent` controls |
| `m_clipPos` | `Vector2` | Top-left of the scissor rectangle |
| `m_clipSize` | `Vector2` | Size of the scissor rectangle |
| `m_fadeAlpha` | `float` | Current opacity multiplier [0.0–1.0]; managed by fade system |
| `m_fade` | `WindowFadeState` | Fade state struct; managed by `ApplyWindowFade` |

### Window Callbacks

| Member | Signature | When Called |
|---|---|---|
| `onCustomRender` | `void(Renderer*)` | After all controls render each frame |
| `onCharInput` | `void(wchar_t)` | On `HandleChar` when this window is focused |
| `onBackspace` | `void()` | On `HandleBackspace` when focused |
| `onEnter` | `void()` | On `HandleEnter` when focused |
| `onMouseWheel` | `void(int)` | On `HandleMouseWheel` when focused; delta is in scroll notches |
| `onCustomMouseInput` | `void(float x, float y)` | On left-click inside window bounds not consumed by any control |

---

## GUIControl — Control Properties

Construct a `GUIControl` on the stack, set its fields, then call `window->AddControl(control)`.

| Field | Type | Default | Description |
|---|---|---|---|
| `id` | `std::string` | `""` | Optional identifier for later lookup via `controls` loop |
| `type` | `GUIControlType` | `None` | Determines rendering and input behaviour |
| `label` | `std::wstring` | `L""` | Text displayed on/in the control |
| `position` | `Vector2` | — | Screen-space top-left position |
| `size` | `Vector2` | — | Width × height in pixels |
| `isVisible` | `bool` | `true` | Hides the control without removing it |
| `bgColor` | `MyColor` | white | Background fill colour (non-hover state) |
| `hoverColor` | `MyColor` | light gray | Background fill colour when hovered |
| `txtColor` | `MyColor` | white | Label text colour |
| `shadowedTxtColor` | `MyColor` | black | Shadow text colour (used if `useShadowedText = true`) |
| `useShadowedText` | `bool` | `false` | Renders a shadow copy of the label 2 px offset |
| `lblFontSize` | `float` | `8.0f` | Font size for the label |
| `lblCenterH` | `bool` | `true` | TitleBar: `true` = H+V centred label, `false` = left-aligned + V centred |
| `bold` | `bool` | `false` | Bold label text (effective on OpenGL; accepted but no-op on DX/Vulkan) |
| `bgTextureId` | `int` | `-1` | Texture ID for the normal state; `-1` = no texture (use colour fill) |
| `bgTextureHoverId` | `int` | `-1` | Texture ID for the hover state |
| `clipContent` | `bool` | `false` | If `true` this control is scissored to the window's `m_clipRect` |
| `sliderMin` | `float` | `0.0f` | HSlider / ToggleSlider minimum value |
| `sliderMax` | `float` | `1.0f` | HSlider / ToggleSlider maximum value |
| `sliderValue` | `float` | `0.0f` | HSlider / ToggleSlider current value |
| `isHovered` | `bool` | `false` | Set by the input system; read-only in callbacks |
| `isPressed` | `bool` | `false` | Set by the input system; read-only in callbacks |
| `isActive` | `bool` | `false` | HSlider: true for the last-touched knob (flashes gold) |

### Control Callbacks

| Member | Signature | Fires When |
|---|---|---|
| `onMouseBtnDown` | `void()` | Mouse button pressed while hovering over the control |
| `onMouseBtnUp` | `void()` | Mouse button released while hovering (completed click) |
| `onMouseOver` | `void()` | Mouse moves over the control |
| `onMouseMove` | `void()` | TitleBar: mouse moves while this control exists |
| `onScroll` | `void(int)` | Scrollbar position changes |
| `onSliderChanged` | `void(float)` | HSlider or ToggleSlider value changes; arg is the new value |

---

## Creating Custom Windows

### Step-by-Step Pattern

```cpp
void CreateMyInventoryWindow() {
    const std::string WIN_NAME = "InventoryWindow";

    // 1. Create the window
    guiManager.CreateMyWindow(
        WIN_NAME,
        GUIWindowType::Standard,
        Vector2(200.0f, 100.0f),
        Vector2(450.0f, 350.0f),
        MyColor(18, 22, 42, 230),
        int(BlitObj2DIndexType::NONE)
    );

    // 2. Get the window pointer
    auto win = guiManager.GetWindow(WIN_NAME);
    if (!win) return;

    // 3. Optional: hide while building to prevent flicker
    win->isVisible = false;

    // 4. Create a weak_ptr for safe lambda captures
    std::weak_ptr<GUIWindow> weakWin = win;

    // 5. Add a TitleBar
    GUIControl titleBar;
    titleBar.type         = GUIControlType::TitleBar;
    titleBar.position     = Vector2(win->position.x, win->position.y);
    titleBar.size         = Vector2(win->size.x - (CLOSEWINBUTTON_SIZE + 6), TITLEBAR_HEIGHT);
    titleBar.bgColor      = MyColor(12, 14, 32, 255);
    titleBar.txtColor     = MyColor(220, 200, 80, 255);
    titleBar.bgTextureId  = int(BlitObj2DIndexType::IMG_TITLEBAR1);
    titleBar.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1HL);
    titleBar.label        = L"Inventory";
    titleBar.lblFontSize  = 16.0f;
    titleBar.isVisible    = true;
    win->AddControl(titleBar);

    // 6. Add a Close button
    GUIControl btnClose;
    btnClose.type         = GUIControlType::Button;
    btnClose.position     = Vector2(
        (win->position.x + win->size.x) - (CLOSEWINBUTTON_SIZE + 4),
        win->position.y + 4);
    btnClose.size         = Vector2(CLOSEWINBUTTON_SIZE, CLOSEWINBUTTON_SIZE);
    btnClose.bgColor      = MyColor(100, 0, 0, 255);
    btnClose.bgTextureId  = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
    btnClose.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
    btnClose.label        = L"";
    btnClose.lblFontSize  = 8.0f;
    btnClose.isVisible    = true;
    btnClose.onMouseBtnDown = [this, windowName = std::string(WIN_NAME)]() {
        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
        RemoveWindow(windowName);
    };
    win->AddControl(btnClose);

    // 7. Add content controls ...
    // (see Control Types section below)

    // 8. Make visible once all controls are added
    win->isVisible = true;
}
```

> **Why hide during construction?**
> The render thread builds a snapshot of `windows` every frame. If `AddControl` reallocates the controls vector while the render thread is iterating it, you get a dangling iterator crash. Setting `isVisible = false` excludes the window from the snapshot until it is fully built.

---

## Control Types — Detailed Guide

### TitleBar

Provides the drag zone and renders the window header.

```cpp
GUIControl tb;
tb.type             = GUIControlType::TitleBar;
tb.position         = Vector2(win->position.x, win->position.y);
tb.size             = Vector2(win->size.x, TITLEBAR_HEIGHT);   // full width
tb.bgColor          = MyColor(0, 0, 0, 255);
tb.txtColor         = MyColor(255, 220, 0, 255);
tb.bgTextureId      = int(BlitObj2DIndexType::IMG_TITLEBAR1);
tb.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1HL);
tb.label            = L"Window Title";
tb.lblFontSize      = 16.0f;
tb.lblCenterH       = true;    // H+V centred (default)
// tb.lblCenterH    = false;   // left-aligned + V centred (e.g. side panels)
tb.isVisible        = true;

// Minimal drag callbacks (GUIWindow::HandleMouseClick sets isDragging automatically)
tb.onMouseBtnDown = [weakWin]() {
    if (auto w = weakWin.lock()) w->isDragging = true;
};
tb.onMouseBtnUp = [weakWin]() {
    if (auto w = weakWin.lock()) w->isDragging = false;
};
win->AddControl(tb);
```

For `GUIWindowType::Dialog` windows, dragging is intentionally disabled (the mouse-move handler ignores drag state for Dialog type).

To suppress the built-in TitleBar rendering entirely and draw a fully custom header via `onCustomRender`, set:
```cpp
tb.bgColor        = MyColor(0, 0, 0, 0);           // transparent
tb.bgTextureId    = int(BlitObj2DIndexType::NONE);  // no texture
tb.label          = L"";                            // no text
```
The control still registers the drag zone; the visual is then owned by `onCustomRender`.

---

### Button

```cpp
GUIControl btn;
btn.type              = GUIControlType::Button;
btn.position          = Vector2(win->position.x + 20, win->position.y + 80);
btn.size              = Vector2(BUTTON_WIDTH, 30);
btn.bgColor           = MyColor(20, 20, 40, 255);
btn.hoverColor        = MyColor(60, 60, 100, 255);
btn.txtColor          = MyColor(220, 220, 220, 255);
btn.bgTextureId       = int(BlitObj2DIndexType::IMG_BUTTONUP1);
btn.bgTextureHoverId  = int(BlitObj2DIndexType::IMG_BUTTON1DOWN);
btn.label             = L"Click Me";
btn.lblFontSize       = 14.0f;
btn.useShadowedText   = true;                       // optional drop-shadow
btn.shadowedTxtColor  = MyColor(0, 0, 0, 200);
btn.isVisible         = true;

// Fires when mouse button is pressed over the button
btn.onMouseBtnDown = [this]() {
    soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
    // ... action on press ...
};

// Fires when mouse button is released while still over the button (completed click)
btn.onMouseBtnUp = [this]() {
    // ... action on release (preferred for UI click completion) ...
};

// Fires every frame the mouse is over the button
btn.onMouseOver = []() {
    // ... hover highlight logic ...
};

win->AddControl(btn);
```

**Button without a texture** (colour-fill only):
```cpp
btn.bgTextureId       = -1;
btn.bgTextureHoverId  = -1;
// bgColor used for normal state; hoverColor used while hovered
```

**Semi-transparent texture button:**

When `bgTextureId != -1`, the texture is tinted with `MyColor(255, 255, 255, bgColor.a)`. Set `bgColor.a < 255` to make a semi-transparent textured button.

---

### TextArea

Renders a labelled rectangle — used for static text labels, info readouts, and content panels.

```cpp
GUIControl area;
area.type             = GUIControlType::TextArea;
area.position         = Vector2(win->position.x + 8, win->position.y + TITLEBAR_HEIGHT + 8);
area.size             = Vector2(win->size.x - 16, win->size.y - TITLEBAR_HEIGHT - 56);
area.bgColor          = MyColor(10, 10, 24, 210);
area.hoverColor       = MyColor(10, 10, 24, 210);  // usually same as bgColor for text areas
area.txtColor         = MyColor(0, 175, 255, 255);
area.bgTextureId      = int(BlitObj2DIndexType::IMG_BEVEL1);
area.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BEVEL1);
area.label            = L"This is the content text.\nSecond line.";
area.lblFontSize      = 14.0f;
area.isVisible        = true;
win->AddControl(area);
```

For a transparent label background:
```cpp
area.bgColor          = MyColor(0, 0, 0, 0);
area.hoverColor       = MyColor(0, 0, 0, 0);
area.bgTextureId      = int(BlitObj2DIndexType::NONE);
area.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
```

To update the label at runtime:
```cpp
auto win = guiManager.GetWindow("MyWindow");
if (win) {
    for (auto& c : win->controls) {
        if (c.id == "status_label") {
            c.label = L"Updated text";
            break;
        }
    }
}
```

---

### HSlider (Horizontal Slider)

```cpp
GUIControl slider;
slider.type             = GUIControlType::HSlider;
slider.id               = "volume_slider";
slider.position         = Vector2(win->position.x + 160, win->position.y + 80);
slider.size             = Vector2(240.0f, 22.0f);
slider.bgColor          = MyColor(0, 0, 0, 0);
slider.hoverColor       = MyColor(0, 0, 0, 0);
slider.bgTextureId      = int(BlitObj2DIndexType::NONE);
slider.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
slider.sliderMin        = 0.0f;
slider.sliderMax        = 64.0f;
slider.sliderValue      = 32.0f;    // starting value
slider.isVisible        = true;

slider.onSliderChanged = [](float v) {
    // v is in [sliderMin, sliderMax]
    int volume = std::clamp((int)std::round(v), 0, 64);
    soundManager.SetMasterVolume(volume);
};
win->AddControl(slider);
```

**Paired readout label pattern** (common in the Config Window):
```cpp
// Add a readout TextArea before the slider with the same id prefix
GUIControl readout;
readout.type      = GUIControlType::TextArea;
readout.id        = "volume_val";
readout.position  = Vector2(win->position.x + 65, win->position.y + 80);
readout.size      = Vector2(85.0f, 22.0f);
readout.bgColor   = MyColor(0, 0, 0, 0);
readout.hoverColor= MyColor(0, 0, 0, 0);
readout.bgTextureId = readout.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
readout.txtColor  = MyColor(200, 200, 200, 255);
readout.label     = L"32";
readout.lblFontSize = 13.0f;
readout.isVisible = true;
win->AddControl(readout);

// Update readout inside the slider callback
slider.onSliderChanged = [weakWin](float v) {
    if (auto w = weakWin.lock()) {
        for (auto& c : w->controls)
            if (c.id == "volume_val") { c.label = std::to_wstring((int)v); break; }
    }
};
```

---

### ToggleSlider

An ON/OFF toggle switch (green knob = ON, red knob = OFF). The `sliderValue` is `1.0f` for ON and `0.0f` for OFF.

```cpp
GUIControl tog;
tog.type             = GUIControlType::ToggleSlider;
tog.id               = "music_toggle";
tog.position         = Vector2(win->position.x + 160, win->position.y + 120);
tog.size             = Vector2(90.0f, 24.0f);
tog.bgColor          = MyColor(0, 0, 0, 0);
tog.hoverColor       = MyColor(0, 0, 0, 0);
tog.bgTextureId      = int(BlitObj2DIndexType::NONE);
tog.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
tog.sliderMin        = 0.0f;
tog.sliderMax        = 1.0f;
tog.sliderValue      = config.myConfig.playMusic ? 1.0f : 0.0f;
tog.isVisible        = true;

tog.onSliderChanged = [](float v) {
    bool on = (v >= 0.5f);
    config.myConfig.playMusic = on;
    config.applyLive();
};
win->AddControl(tog);
```

To read the current state at any point:
```cpp
bool isOn = (control.sliderValue >= 0.5f);
```

---

### Scrollbar

A vertical scrollbar thumb. The thumb position is set by mouse click; the `onScroll` callback receives the new pixel offset.

```cpp
GUIControl scrollbar;
scrollbar.type        = GUIControlType::Scrollbar;
scrollbar.position    = Vector2(win->position.x + win->size.x - SCROLLBAR_WIDTH - 4,
                                win->position.y + TITLEBAR_HEIGHT + 4);
scrollbar.size        = Vector2(SCROLLBAR_WIDTH, win->size.y - TITLEBAR_HEIGHT - 8);
scrollbar.bgColor     = MyColor(40, 40, 60, 200);
scrollbar.hoverColor  = MyColor(60, 60, 90, 255);
scrollbar.bgTextureId = int(BlitObj2DIndexType::NONE);
scrollbar.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
scrollbar.isVisible   = true;

scrollbar.onScroll = [weakWin](int newPosition) {
    if (auto w = weakWin.lock())
        w->scrollPosition = newPosition;
};
win->AddControl(scrollbar);
```

For smooth scrollable content, prefer the `onMouseWheel` + `onCustomRender` pattern used by the Config Window (see [Scrollable Content and Clip Rects](#scrollable-content-and-clip-rects)).

---

## Input Handling

### The `isLeftClick` Flag

`HandleAllInput` receives a `bool& isLeftClick` that is `true` while the left mouse button is held and `false` when it is up. It is **not** a one-shot edge trigger. The input system detects transitions internally:

- **`onMouseBtnDown`** fires once when `isLeftClick` goes `false → true` while hovering
- **`onMouseBtnUp`** fires once when `isLeftClick` goes `true → false` while still hovering

### Click Cooldown

A 1-second cross-frame click cooldown (`kClickCooldown = 1000 ms`) prevents accidental double-fires when two windows overlap or when a button closes a window and the next window would immediately receive the release. The cooldown is acquired by the first button that fires `onMouseBtnDown` and released automatically after 1 second.

### Focus Model

Only the **topmost visible window** (highest `zOrder`) receives input. All other windows have their hover and pressed states cleared every frame. Clicks outside the focused window's bounds are silently consumed — they do not pass through to background windows.

To give a window input focus, call:
```cpp
guiManager.BringWindowToFront("MyWindow");
```

### Active Interaction Continuation

If the user is dragging a title bar or pressing a slider knob and the mouse leaves the window bounds, the gesture continues until the mouse button is released. This prevents sliders from freezing mid-drag.

---

## Keyboard and Mouse Wheel Routing

Register callbacks on the window that should receive keyboard/wheel events:

```cpp
auto win = guiManager.GetWindow("ConsoleWindow");
if (!win) return;

// Character input (e.g. for a text entry field)
win->onCharInput = [weakWin](wchar_t c) {
    if (auto w = weakWin.lock())
        w->contentText += c;
};

// Backspace
win->onBackspace = [weakWin]() {
    if (auto w = weakWin.lock() && !w->contentText.empty())
        w->contentText.pop_back();
};

// Enter / confirm
win->onEnter = [weakWin]() {
    if (auto w = weakWin.lock()) {
        ProcessCommand(w->contentText);
        w->contentText.clear();
    }
};

// Mouse wheel scroll
win->onMouseWheel = [weakWin](int delta) {
    if (auto w = weakWin.lock())
        w->scrollPosition = std::clamp(w->scrollPosition - delta * 3, 0, w->maxScrollPosition);
};
```

Events are dispatched by `GUIManager::HandleChar`, `HandleBackspace`, `HandleEnter`, and `HandleMouseWheel` — call these from your platform's WndProc or input handler.

---

## Window Management

### Z-Order

Windows are assigned a monotonically-increasing `zOrder` at creation time. Higher values render on top and receive exclusive input. `BringWindowToFront` assigns the next available z-order value, placing the window above all existing windows.

```cpp
guiManager.BringWindowToFront("ConsoleWindow");
```

### Visibility Toggle

```cpp
// Instant hide/show (no fade)
guiManager.SetWindowVisibility("HUDWindow", false);
guiManager.SetWindowVisibility("HUDWindow", true);
```

### Resize Handling

```cpp
// Call this whenever the renderer resizes
guiManager.OnWindowResize(newWidth, newHeight);
```

The built-in implementation repositions `GameMenuWindow`. For other windows that must track screen edges, update their `position` in a resize callback:

```cpp
void OnResize(int w, int h) {
    guiManager.OnWindowResize(w, h);
    auto hud = guiManager.GetWindow("HUDWindow");
    if (hud) hud->position = Vector2((float)w - hud->size.x - 10.0f, 10.0f);
}
```

---

## Window Fade System

Two non-blocking methods perform alpha-transparency fade transitions. The window's `m_fadeAlpha` multiplier is applied to every draw call inside `GUIWindow::Render()`, so the entire window — background, all controls, all text — fades uniformly. This works identically across all four render pipelines.

### `ApplyWindowFade` — Fire and Forget

```cpp
void GUIManager::ApplyWindowFade(
    GUIWindowFadeType winfadeType,
    float overTimePeriod,
    const std::string& windowName);
```

Starts a fade and completes silently.

```cpp
// Fade in a window over 0.5 seconds
guiManager.ApplyWindowFade(GUIWindowFadeType::FadeIn, 0.5f, "InventoryWindow");

// Fade out a window over 1.0 second (sets isVisible=false when done)
guiManager.ApplyWindowFade(GUIWindowFadeType::FadeOut, 1.0f, "InventoryWindow");
```

### `ApplyWindowFadeCallback` — With Completion Lambda

```cpp
void GUIManager::ApplyWindowFadeCallback(
    GUIWindowFadeType winfadeType,
    float overTimePeriod,
    const std::string& windowName,
    std::function<void()> callback);
```

Fires the lambda when the fade completes. The callback is dispatched from `GUIManager::Render()` **after** the mutex is released, so it is safe to call any `GUIManager` method (including starting another fade) from inside it.

```cpp
// Fade out, then remove the window
guiManager.ApplyWindowFadeCallback(
    GUIWindowFadeType::FadeOut, 0.8f, "SplashWindow",
    [this]() {
        RemoveWindow("SplashWindow");
        guiManager.ApplyWindowFade(GUIWindowFadeType::FadeIn, 0.5f, "MainMenuWindow");
    });
```

### Fade Behaviour Summary

| Fade Type | `isVisible` on start | `m_fadeAlpha` on start | On completion |
|---|---|---|---|
| `FadeIn` | Set to `true` | Set to `0.0f` | `m_fadeAlpha` → `1.0f`; `isVisible` stays `true` |
| `FadeOut` | Stays `true` | Set to `1.0f` | `isVisible` → `false`; `m_fadeAlpha` → `1.0f` (reset for next show) |

### Chain Fade Example

```cpp
// Scene transition: fade out existing UI, switch scene, fade in new UI
void TransitionScene() {
    guiManager.ApplyWindowFadeCallback(
        GUIWindowFadeType::FadeOut, 1.0f, "GameHUD",
        [this]() {
            RemoveWindow("GameHUD");
            scene.SetGotoScene(SCENE_MENU);
            scene.InitiateScene();
            // Create new menu window then fade it in
            CreateMainMenuWindow();
            guiManager.ApplyWindowFade(GUIWindowFadeType::FadeIn, 0.5f, "MainMenuWindow");
        });
}
```

### Instant Show / Instant Hide

Pass `0.0f` as `overTimePeriod` for an immediate transition (the fade completes in the very next `Render()` tick):
```cpp
guiManager.ApplyWindowFade(GUIWindowFadeType::FadeIn,  0.0f, "HUDWindow");
guiManager.ApplyWindowFade(GUIWindowFadeType::FadeOut, 0.0f, "HUDWindow");
```

---

## Custom Rendering Extension

Attach `onCustomRender` to draw anything that cannot be expressed through the standard `GUIControl` set. It fires at the end of `GUIWindow::Render()`, after all controls, on the renderer's current draw context.

```cpp
win->onCustomRender = [weakWin](Renderer* r) {
    auto w = weakWin.lock();
    if (!w) return;

    // Draw a progress bar manually
    float barX = w->position.x + 10.0f;
    float barY = w->position.y + w->size.y - 30.0f;
    float barW = (w->size.x - 20.0f) * progress;   // 'progress' captured from enclosing scope
    r->DrawRectangle(Vector2(barX, barY), Vector2(w->size.x - 20.0f, 16.0f),
                     MyColor(30, 30, 50, 200), true);
    r->DrawRectangle(Vector2(barX, barY), Vector2(barW, 16.0f),
                     MyColor(60, 200, 80, 230), true);

    // Draw a circle
    r->DrawCircle(Vector2(w->position.x + 20.0f, w->position.y + 20.0f),
                  10.0f, MyColor(255, 80, 80, 220), true);

    // Draw centred text
    r->DrawMyTextCentered(L"Loading...",
        Vector2(w->position.x, w->position.y + w->size.y - 50.0f),
        MyColor(200, 200, 200, 255), 14.0f,
        w->size.x, 20.0f);
};
```

> Positions inside `onCustomRender` should be computed from `w->position` (the live window position) so they remain correct after window dragging. Do not hard-code screen coordinates.

> The `onCustomRender` callback is invoked **outside** any scissor rect (the clip rect is always popped before calling it), so custom content can draw anywhere on screen without being clipped.

---

## Custom Input Extensions

### `onCustomMouseInput`

Fires on a left-click inside the window bounds that was **not consumed** by any registered `GUIControl`. Useful for custom hit regions (e.g., a scrollbar drawn by `onCustomRender`).

```cpp
win->onCustomMouseInput = [weakWin, setTabScroll, SCROLL_H, CFG_SCROLL_W](float mx, float my) {
    auto w = weakWin.lock();
    if (!w) return;

    // Determine if the click hit the custom scrollbar track
    float scrollX = w->position.x + w->size.x - CFG_SCROLL_W - 7.0f;
    float scrollY = w->position.y + TITLEBAR_HEIGHT + 30.0f;
    if (mx < scrollX || mx > scrollX + CFG_SCROLL_W) return;
    if (my < scrollY || my > scrollY + SCROLL_H)     return;

    float rel    = (my - scrollY) / SCROLL_H;
    float target = rel * maxScrollRange;
    setTabScroll(target - currentScrollOffset);
};
```

### `onCharInput`, `onBackspace`, `onEnter`

```cpp
// Text entry field
std::wstring inputBuffer;

win->onCharInput = [&inputBuffer](wchar_t c) {
    if (c >= L' ') inputBuffer += c;    // Ignore control characters
};
win->onBackspace = [&inputBuffer]() {
    if (!inputBuffer.empty()) inputBuffer.pop_back();
};
win->onEnter = [this, &inputBuffer]() {
    SubmitInput(inputBuffer);
    inputBuffer.clear();
};
```

---

## Scrollable Content and Clip Rects

For windows with more content than fits in the visible area, use the scissor / clip rect system combined with per-control `y` position shifting.

```cpp
// 1. Mark content controls with clipContent = true and a shared id prefix
GUIControl row;
row.id          = "tab0_myrow";     // prefix 'tab0_' marks this as tab 0 content
row.clipContent = true;
// ... set other fields ...
win->AddControl(row);

// 2. Set the window's clip rectangle to the content area
win->m_hasClip  = true;
win->m_clipPos  = Vector2(WX, contentAreaY);
win->m_clipSize = Vector2(WW, contentAreaHeight);

// 3. Handle mouse wheel to shift visible controls
win->onMouseWheel = [weakWin](int delta) {
    if (auto w = weakWin.lock()) {
        float shift = static_cast<float>(-delta) * 18.0f;   // 18 px per notch
        // Move all tab content controls up or down
        for (auto& c : w->controls) {
            if (c.id.rfind("tab0_", 0) == 0) {   // starts with "tab0_"
                c.position.y -= shift;
                // Cull controls completely outside the content area
                c.isVisible = !((c.position.y + c.size.y <= contentAreaY) ||
                                (c.position.y >= contentAreaY + contentAreaHeight));
            }
        }
    }
};
```

When `m_hasClip` is `true`, `GUIWindow::Render()` wraps all `clipContent` controls inside `renderer->PushClipRect` / `PopClipRect`. This produces pixel-accurate scissoring on all four pipelines.

> The `container` control (the bevel background) should **not** have `clipContent = true` — it renders behind the content and should never be scissored.

---

## Modal Windows

A modal window blocks all other windows from receiving input while it is visible.

```cpp
win->isModal = true;    // set before or after AddControl
```

Only one window needs `isModal = true`. When `HandleAllInput` detects any visible modal window, it routes input exclusively to that window and strips hover/pressed states from all other windows.

```cpp
// Typical confirmation dialog pattern
void ShowConfirmDialog(std::function<void()> onConfirm) {
    guiManager.CreateMyWindow("ConfirmDialog", GUIWindowType::Dialog,
        Vector2(300, 200), Vector2(300, 150),
        MyColor(0, 0, 0, 230), int(BlitObj2DIndexType::NONE));

    auto win = guiManager.GetWindow("ConfirmDialog");
    if (!win) return;
    win->isModal = true;

    GUIControl yesBtn;
    yesBtn.type = GUIControlType::Button;
    // ... position, size, label ...
    yesBtn.onMouseBtnDown = [this, onConfirm]() {
        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
        RemoveWindow("ConfirmDialog");
        onConfirm();
    };
    win->AddControl(yesBtn);

    GUIControl noBtn;
    noBtn.type = GUIControlType::Button;
    // ... position, size, label ...
    noBtn.onMouseBtnDown = [this]() {
        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
        RemoveWindow("ConfirmDialog");
    };
    win->AddControl(noBtn);
}
```

---

## Built-in Window Factories

The engine ships three pre-built windows. Call these on `GUIManager` directly.

### `CreateAlertWindow`

```cpp
guiManager.CreateAlertWindow(L"Your save file could not be loaded!\nStarting with defaults.");
```

Creates a draggable alert popup with a title bar, text area displaying the message, and an OK/Close button. The window is named `"AlertWindow"`.

### `CreateGameMenuWindow`

```cpp
guiManager.CreateGameMenuWindow(L"");
```

Creates the full-screen right-side game menu panel (named `"GameMenuWindow"`) with Configuration, Game Play, High Scores, Credits, and Quit buttons. Also adds an `** EXPERIMENTAL **` button in `_DEBUG` builds. The menu docks to the right edge and resizes vertically with the window.

### `CreateConfigWindow`

```cpp
guiManager.CreateConfigWindow();
```

Opens the tabbed System Configuration dialog (named `"ConfigWindow"`) covering Game Play, Audio, Video, Controls, and Key Mapping settings. Modal. Includes live scrolling, per-tab clip rects, a custom-rendered 3D title bar, a circular close button, and a video-restart notification flow.

---

## Thread Safety

`GUIManager` uses a `std::timed_mutex` (`mutex`) to serialise all access to `windows`. Key rules:

- **`Render()`** acquires a blocking `lock_guard` for snapshot building and fade ticking, then releases before any rendering or callback dispatch.
- **`RemoveWindow()`** uses a 16 ms timed lock so it does not permanently deadlock if the render thread is slow.
- **`HandleAllInput()`** uses an 8 ms timed lock for the same reason.
- **Fade callbacks** fire **after** the mutex is released in `Render()`, so they may safely call `CreateMyWindow`, `RemoveWindow`, `ApplyWindowFade`, or any other `GUIManager` method.
- **`GetWindow()`** is **not** mutex-guarded. Call it only from the thread that owns the window creation/removal context (usually the main/game thread). Do not call `GetWindow` from the render thread.
- **Control `label` updates** (e.g., `c.label = L"New text"`) are safe from the main thread between frames but must not happen concurrently with `GUIWindow::Render()`. If you must update from a background thread, use the window's `mutex` (accessible via `friend class GUIManager`).

---

## Performance Considerations

- **Window count** — the render path iterates all windows and sorts by z-order every frame. Keep the live window count low (fewer than 20 is typical). Remove windows that are no longer needed.
- **Control count** — each frame every control in every visible window is rendered. Avoid adding hundreds of controls; group static content into `TextArea` with `onCustomRender` instead.
- **Texture vs colour** — textured controls (`bgTextureId != -1`) draw a `DrawTexture` call; colour-fill controls draw a `DrawRectangle`. Both are fast; mixing is fine.
- **`isVisible = false`** — invisible controls are skipped in the render loop but still exist in memory. For large lists (e.g., inventory), hide unused rows rather than adding/removing them.
- **Fade alpha** — the `fc()` lambda in `GUIWindow::Render()` multiplies every colour's alpha by `m_fadeAlpha`. When `m_fadeAlpha == 1.0f` (no fade active), the multiplication still runs but costs nothing measurable at 60 fps.

---

## Common Pitfalls

### Window not visible after creation

`CreateMyWindow` sets `isVisible = true` by default. If you set `win->isVisible = false` during construction (to prevent render-thread race), **remember to set it back to `true`** after `AddControl` calls are complete.

### Lambda capturing `this` after `RemoveWindow`

```cpp
// WRONG — 'this' (GUIControl*) becomes dangling when RemoveWindow clears controls
btn.onMouseBtnDown = [this]() { ... };

// CORRECT — capture by value
btn.onMouseBtnDown = [this, windowName = std::string("MyWindow")]() {
    RemoveWindow(windowName);
};
```

### Capturing `GUIControl&` from a lambda

Controls live in a `std::vector<GUIControl>`. Any `AddControl` call may reallocate the vector, invalidating all existing references. **Never capture `control` or `&control` in a lambda.** Capture the window via `std::weak_ptr<GUIWindow>` and search by `id` at callback time.

### `GetWindow` returns null after `RemoveWindow`

`RemoveWindow` erases the window from the map immediately. Any subsequent `GetWindow` call returns an empty pointer. Check before dereferencing:

```cpp
auto win = guiManager.GetWindow("MyWindow");
if (!win || win->bWindowDestroy) return;
```

### Controls added in wrong order create z-fighting

`GUIWindow::Render()` renders controls in `controls` vector order. Add background controls (bevel, TextArea panels) before foreground controls (buttons, sliders) so foreground controls paint on top.

### Dragging a `GUIWindowType::Dialog` window

`HandleMouseMove` deliberately skips drag logic for `Dialog` type windows. If you want a dialog to be draggable, use `GUIWindowType::Standard` instead.

### Fade callback fires before window is ready

If you `ApplyWindowFadeCallback(FadeOut)` on a window you then immediately call `RemoveWindow` on in the same frame, the window will already be destroyed before the callback fires (which is a no-op because `bWindowDestroy` prevents the fade tick). Let the callback call `RemoveWindow` instead:

```cpp
// CORRECT pattern for fade-then-remove
guiManager.ApplyWindowFadeCallback(GUIWindowFadeType::FadeOut, 0.5f, "MyWindow",
    [this]() { RemoveWindow("MyWindow"); });
// DO NOT call RemoveWindow here
```

---

## Complete Working Examples

### Example 1 — Simple Notification Banner

```cpp
void ShowNotificationBanner(const std::wstring& message) {
    const std::string WIN = "NotificationBanner";
    if (guiManager.GetWindow(WIN)) return;   // already showing

    guiManager.CreateMyWindow(WIN, GUIWindowType::Dialog,
        Vector2(renderer->iOrigWidth * 0.5f - 200.0f, 10.0f),
        Vector2(400.0f, 50.0f),
        MyColor(0, 0, 0, 200),
        int(BlitObj2DIndexType::NONE));

    auto win = guiManager.GetWindow(WIN);
    if (!win) return;
    win->isVisible = false;
    win->isModal   = false;

    GUIControl label;
    label.type        = GUIControlType::TextArea;
    label.position    = Vector2(win->position.x + 8, win->position.y + 10);
    label.size        = Vector2(win->size.x - 16, 30.0f);
    label.bgColor     = MyColor(0, 0, 0, 0);
    label.hoverColor  = MyColor(0, 0, 0, 0);
    label.bgTextureId = label.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
    label.txtColor    = MyColor(255, 220, 80, 255);
    label.label       = message;
    label.lblFontSize = 14.0f;
    label.isVisible   = true;
    win->AddControl(label);

    win->isVisible = true;

    // Fade in over 0.3 s, stay for ~2 s, then fade out and remove
    guiManager.ApplyWindowFadeCallback(GUIWindowFadeType::FadeIn, 0.3f, WIN,
        [this, WIN]() {
            // After fade-in completes, wait 2 seconds then fade out
            std::thread([this, WIN]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                guiManager.ApplyWindowFadeCallback(GUIWindowFadeType::FadeOut, 0.5f, WIN,
                    [this, WIN]() { RemoveWindow(WIN); });
            }).detach();
        });
}
```

---

### Example 2 — Settings Panel with Slider and Toggle

```cpp
void CreateSettingsPanel() {
    const std::string WIN = "SettingsPanel";
    if (guiManager.GetWindow(WIN)) return;

    const float WX = 80.0f, WY = 80.0f, WW = 500.0f, WH = 260.0f;

    guiManager.CreateMyWindow(WIN, GUIWindowType::Standard,
        Vector2(WX, WY), Vector2(WW, WH),
        MyColor(15, 18, 35, 230),
        int(BlitObj2DIndexType::NONE));

    auto win = guiManager.GetWindow(WIN);
    if (!win) return;
    win->isVisible = false;
    win->isModal   = true;

    std::weak_ptr<GUIWindow> weakWin = win;

    // --- Title Bar ---
    GUIControl tb;
    tb.type = GUIControlType::TitleBar;
    tb.position = Vector2(WX, WY);
    tb.size     = Vector2(WW - (CLOSEWINBUTTON_SIZE + 6), TITLEBAR_HEIGHT);
    tb.bgColor  = MyColor(10, 12, 28, 255);
    tb.txtColor = MyColor(220, 200, 80, 255);
    tb.bgTextureId = tb.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1);
    tb.label = L"Settings";  tb.lblFontSize = 16.0f;  tb.isVisible = true;
    tb.onMouseBtnDown = [weakWin]() { if (auto w = weakWin.lock()) w->isDragging = true;  };
    tb.onMouseBtnUp   = [weakWin]() { if (auto w = weakWin.lock()) w->isDragging = false; };
    win->AddControl(tb);

    // --- Close Button ---
    GUIControl btnX;
    btnX.type = GUIControlType::Button;
    btnX.position = Vector2(WX + WW - CLOSEWINBUTTON_SIZE - 4, WY + 4);
    btnX.size     = Vector2(CLOSEWINBUTTON_SIZE, CLOSEWINBUTTON_SIZE);
    btnX.bgColor = MyColor(100, 0, 0, 255);
    btnX.bgTextureId = btnX.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
    btnX.label = L"";  btnX.isVisible = true;
    btnX.onMouseBtnDown = [this, WIN]() {
        config.myConfig = revertConfig;   // revert on cancel
        config.applyLive();
        RemoveWindow(WIN);
    };
    win->AddControl(btnX);

    // --- Volume Slider ---
    const float CX = WX + 10.0f, CY = WY + TITLEBAR_HEIGHT + 20.0f;
    const float ROW = 36.0f;

    // Label
    GUIControl volLbl;
    volLbl.type = GUIControlType::TextArea;
    volLbl.id = "vol_lbl";
    volLbl.position = Vector2(CX, CY);
    volLbl.size     = Vector2(160.0f, 26.0f);
    volLbl.bgColor = volLbl.hoverColor = MyColor(0, 0, 0, 0);
    volLbl.bgTextureId = volLbl.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
    volLbl.txtColor = MyColor(200, 200, 200, 255);
    volLbl.label = L"Master Volume:";
    volLbl.lblFontSize = 13.0f;  volLbl.isVisible = true;
    win->AddControl(volLbl);

    // Readout
    GUIControl volVal;
    volVal.type = GUIControlType::TextArea;
    volVal.id = "vol_val";
    volVal.position = Vector2(CX + 165.0f, CY);
    volVal.size     = Vector2(70.0f, 26.0f);
    volVal.bgColor = volVal.hoverColor = MyColor(0, 0, 0, 0);
    volVal.bgTextureId = volVal.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
    volVal.txtColor = MyColor(200, 200, 200, 255);
    volVal.label = std::to_wstring(config.myConfig.masterVolume);
    volVal.lblFontSize = 13.0f;  volVal.isVisible = true;
    win->AddControl(volVal);

    // Slider
    GUIControl volSldr;
    volSldr.type = GUIControlType::HSlider;
    volSldr.id   = "vol_sldr";
    volSldr.position = Vector2(CX + 245.0f, CY + 2.0f);
    volSldr.size     = Vector2(WW - 265.0f, 22.0f);
    volSldr.bgColor = volSldr.hoverColor = MyColor(0, 0, 0, 0);
    volSldr.bgTextureId = volSldr.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
    volSldr.sliderMin = 0.0f;  volSldr.sliderMax = 64.0f;
    volSldr.sliderValue = (float)config.myConfig.masterVolume;
    volSldr.isVisible = true;
    volSldr.onSliderChanged = [weakWin](float v) {
        int vol = std::clamp((int)std::round(v), 0, 64);
        config.myConfig.masterVolume = vol;
        config.applyLive();
        if (auto w = weakWin.lock())
            for (auto& c : w->controls)
                if (c.id == "vol_val") { c.label = std::to_wstring(vol); break; }
    };
    win->AddControl(volSldr);

    // --- Music Toggle ---
    GUIControl musLbl;
    musLbl.type = GUIControlType::TextArea;
    musLbl.id = "mus_lbl";
    musLbl.position = Vector2(CX, CY + ROW);
    musLbl.size     = Vector2(160.0f, 26.0f);
    musLbl.bgColor = musLbl.hoverColor = MyColor(0, 0, 0, 0);
    musLbl.bgTextureId = musLbl.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
    musLbl.txtColor = MyColor(200, 200, 200, 255);
    musLbl.label = L"Play Music:";
    musLbl.lblFontSize = 13.0f;  musLbl.isVisible = true;
    win->AddControl(musLbl);

    GUIControl musTog;
    musTog.type = GUIControlType::ToggleSlider;
    musTog.id   = "mus_tog";
    musTog.position = Vector2(CX + 165.0f, CY + ROW);
    musTog.size     = Vector2(90.0f, 26.0f);
    musTog.bgColor = musTog.hoverColor = MyColor(0, 0, 0, 0);
    musTog.bgTextureId = musTog.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
    musTog.sliderMin = 0.0f;  musTog.sliderMax = 1.0f;
    musTog.sliderValue = config.myConfig.playMusic ? 1.0f : 0.0f;
    musTog.isVisible = true;
    musTog.onSliderChanged = [](float v) {
        config.myConfig.playMusic = (v >= 0.5f);
        config.applyLive();
    };
    win->AddControl(musTog);

    // --- Save Button ---
    GUIControl saveBtn;
    saveBtn.type = GUIControlType::Button;
    saveBtn.position = Vector2(WX + 10.0f, WY + WH - 44.0f);
    saveBtn.size     = Vector2(140.0f, 34.0f);
    saveBtn.bgColor  = MyColor(20, 20, 35, 200);
    saveBtn.hoverColor = MyColor(60, 60, 90, 255);
    saveBtn.bgTextureId = saveBtn.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    saveBtn.txtColor = MyColor(210, 210, 210, 255);
    saveBtn.label = L"Save";  saveBtn.lblFontSize = 14.0f;  saveBtn.isVisible = true;
    saveBtn.onMouseBtnDown = [this, WIN]() {
        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
        config.saveConfig();
        RemoveWindow(WIN);
    };
    win->AddControl(saveBtn);

    win->isVisible = true;
    guiManager.ApplyWindowFade(GUIWindowFadeType::FadeIn, 0.3f, WIN);
}
```

---

### Example 3 — Custom-Rendered Progress Dialog

```cpp
void CreateProgressDialog(const std::wstring& task, float& progressRef) {
    const std::string WIN = "ProgressDialog";
    guiManager.CreateMyWindow(WIN, GUIWindowType::Dialog,
        Vector2(300.0f, 250.0f), Vector2(400.0f, 100.0f),
        MyColor(0, 0, 0, 210),
        int(BlitObj2DIndexType::NONE));

    auto win = guiManager.GetWindow(WIN);
    if (!win) return;
    win->isModal   = true;
    win->isVisible = false;

    std::weak_ptr<GUIWindow> weakWin = win;
    float* pProgress = &progressRef;   // pointer into caller's variable

    // TitleBar (transparent — custom render draws the header)
    GUIControl tb;
    tb.type = GUIControlType::TitleBar;
    tb.position = Vector2(300.0f, 250.0f);
    tb.size     = Vector2(400.0f, TITLEBAR_HEIGHT);
    tb.bgColor  = MyColor(0, 0, 0, 0);
    tb.bgTextureId = tb.bgTextureHoverId = int(BlitObj2DIndexType::NONE);
    tb.label = L"";  tb.isVisible = true;
    win->AddControl(tb);

    // Custom renderer draws everything
    win->onCustomRender = [weakWin, task, pProgress](Renderer* r) {
        auto w = weakWin.lock();
        if (!w) return;

        const float wx = w->position.x, wy = w->position.y;
        const float ww = w->size.x;

        // Header
        r->DrawRectangle(Vector2(wx, wy), Vector2(ww, TITLEBAR_HEIGHT),
                         MyColor(15, 18, 38, 255), true);
        r->DrawMyTextCentered(task, Vector2(wx, wy),
                              MyColor(220, 200, 80, 255), 14.0f, ww, TITLEBAR_HEIGHT);

        // Progress bar background
        float barY = wy + TITLEBAR_HEIGHT + 24.0f;
        r->DrawRectangle(Vector2(wx + 10.0f, barY), Vector2(ww - 20.0f, 20.0f),
                         MyColor(20, 20, 35, 220), true);

        // Filled portion
        float filled = std::clamp(*pProgress, 0.0f, 1.0f) * (ww - 20.0f);
        if (filled > 0.5f)
            r->DrawRectangle(Vector2(wx + 10.0f, barY), Vector2(filled, 20.0f),
                             MyColor(40, 160, 60, 230), true);

        // Percentage text
        wchar_t pct[16];
        swprintf_s(pct, L"%.0f%%", *pProgress * 100.0f);
        r->DrawMyTextCentered(pct, Vector2(wx, barY),
                              MyColor(255, 255, 255, 255), 12.0f, ww, 20.0f);
    };

    win->isVisible = true;
    guiManager.ApplyWindowFade(GUIWindowFadeType::FadeIn, 0.2f, WIN);
}
```

---

*End of GUIManager-Example-Usage.md*
