#pragma once

#define NOMINMAN

#include "Includes.h"
#include "RendererMacros.h"
#include "Renderer.h"
#include "DX11Renderer.h"
#include "GUIManager.h"
#include "SoundManager.h"
#include "Debug.h"

extern SoundManager soundManager;
extern HWND hwnd;
extern Debug debug;

GUIWindow::GUIWindow(const std::string& name, GUIWindowType type, const Vector2& position,
    const Vector2& size, const MyColor& backgroundColor, int backgroundTextureId, Renderer* renderer)
    : name(name), type(type), position(position), size(size), backgroundColor(backgroundColor),
    backgroundTextureId(backgroundTextureId), myRenderer(myRenderer)
{
    // Ensure proper initialization of all required properties
    controls.clear(); // Ensure controls list is empty initially
    isVisible = true; // Default visibility
}

void GUIWindow::AddControl(const GUIControl& control) 
{
    controls.push_back(control);
}

// GUIManager Implementation
GUIManager::GUIManager()
{
    //SecureZeroMemory(&buffer, sizeof(buffer));
}

GUIManager::~GUIManager()
{
    if (bHasCleanedUp) { return; }
    windows.clear();
    debug.Log("GUIManager cleaned up.\n");
    bHasCleanedUp = true;
}

void GUIManager::Initialize(Renderer* renderer) {
    this->myRenderer = renderer; // Store the renderer
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Initializing GUIManager...\n");
}

void GUIManager::CreateMyWindow(const std::string& name, GUIWindowType type, const Vector2& position, const Vector2& size,
    const MyColor& backgroundColor, int backgroundTextureId) {
    if (windows.find(name) != windows.end()) {
        debug.LogError("Window with name '" + name + "' already exists.\n");
        return;
    }

    // Create the window and pass the stored renderer
    std::shared_ptr<GUIWindow> window = std::make_shared<GUIWindow>(name, type, position, size, backgroundColor, backgroundTextureId, this->myRenderer);
    windows[name] = window;
    window->myRenderer = this->myRenderer;
    debug.Log("Window '" + name + "' created.\n");
}

void GUIManager::RemoveWindow(const std::string& name) {
    // Attempt to lock the mutex without blocking
    if (!mutex.try_lock()) {
    }
    else
    {
        // We got the lock - proceed
        std::lock_guard<std::mutex> lock(mutex, std::adopt_lock); // Adopt the existing lock
    }

    auto it = windows.find(name);
    if (it != windows.end()) {
        // Clear all controls in the window
        if (it->second->bWindowDestroy)
            return;

        it->second->bWindowDestroy = true;
        it->second->controls.clear();

        // Remove the window from the map
        windows.erase(it);

        // Log the removal
        debug.Log("Window '" + name + "' removed.\n");
    }
    else {
        // Log an error if the window doesn't exist
        debug.LogError("Failed to remove window '" + name + "'. It does not exist.\n");
    }
}

void GUIManager::Render() {
    if (!myRenderer) return; // Ensure the renderer is valid

    for (const auto& [name, window] : windows) {
        if (window->isVisible) {
            window->Render(); // No need to pass the renderer
        }
    }
}

void GUIManager::HandleAllInput(const Vector2& mousePosition, bool& isLeftClick) {
    std::lock_guard<std::mutex> lock(mutex);  // Ensure thread safety

    // Create a local copy of windows to avoid issues if windows are removed during iteration
    auto windowsCopy = windows;

    for (const auto& windowPair : windowsCopy) {
        if (!windowPair.second) continue;  // Skip if window is null

        // Check window state before processing
        if (windowPair.second->bWindowDestroy) continue;
        if (!windowPair.second->isVisible) continue;

        // Process input
        windowPair.second->HandleMouseClick(mousePosition, isLeftClick);
        windowPair.second->HandleMouseMove(mousePosition, windows);
    }
}

void GUIManager::HandleInput(const std::string& windowName, const Vector2& mousePosition, bool& isLeftClick) {
    std::shared_ptr<GUIWindow> window = GetWindow(windowName);
    if (!window || !window->isVisible || window->bWindowDestroy) return;

    window->HandleMouseClick(mousePosition, isLeftClick);
    window->HandleMouseMove(mousePosition, windows);
}

void GUIManager::SetWindowVisibility(const std::string& name, bool isVisible) {
    std::shared_ptr<GUIWindow> window = GetWindow(name);
    if (window) {
        window->isVisible = isVisible;

        std::snprintf(buffer, sizeof(buffer), "Window '%s' visibility set to %d.\n", name.c_str(), isVisible);
        debug.Log(buffer);
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "Window '%s' does not exist!\n", name.c_str());
        debug.Log(buffer);
    }
}

std::shared_ptr<GUIWindow> GUIManager::GetWindow(const std::string& name) {
    auto it = windows.find(name);
    if (it != windows.end()) {
        return it->second;
    }
    std::snprintf(buffer, sizeof(buffer), "Window '%s' not found.\n", name.c_str());
    return std::shared_ptr<GUIWindow>();
}

void GUIWindow::HandleMouseMove(const Vector2& mousePosition, const std::unordered_map<std::string, std::shared_ptr<GUIWindow>>& allWindows) {
    if (bWindowDestroy || !isVisible) return;

    for (auto& control : controls) {
        bool isMouseOver =
            mousePosition.x >= control.position.x &&
            mousePosition.x <= control.position.x + control.size.x &&
            mousePosition.y >= control.position.y &&
            mousePosition.y <= control.position.y + control.size.y;

        control.isHovered = isMouseOver;
    
        switch (control.type)
        {
            case GUIControlType::TitleBar:
            {
                if ((isDragging) && (this->type != GUIWindowType::Dialog))
                {
                    Vector2 delta = mousePosition - dragStartMousePosition;
                    Vector2 newPos = dragStartPosition + delta;
                    MoveWindow(newPos, allWindows); 
                }

                break;
            }

            case GUIControlType::Button:
            {
                if (isMouseOver)
                {
                   if (control.onMouseOver) control.onMouseOver();
                }

                break;
            }

            default:
                break;
        }
    }
}

void GUIWindow::HandleMouseClick(const Vector2& mousePosition, bool& isLeftClick) {
    if (bWindowDestroy) return;

    for (auto& control : controls) {
        bool isMouseOver =
            mousePosition.x >= control.position.x &&
            mousePosition.x <= control.position.x + control.size.x &&
            mousePosition.y >= control.position.y &&
            mousePosition.y <= control.position.y + control.size.y;

        control.isHovered = isMouseOver;

        switch (control.type) {
        case GUIControlType::TitleBar:
        {
            if (isMouseOver && isLeftClick && !isDragging) {
                // Start dragging
                isDragging = true;
                dragStartMousePosition = mousePosition;
                dragStartPosition = position;
                control.isPressed = true;
                SetCapture(hwnd); // capture the mouse
            }
            else if (!isLeftClick && isDragging) {
                // Stop dragging
                isDragging = false;
                control.isPressed = false;
                ReleaseCapture(); // release mouse capture
            }
            break;
        }

        case GUIControlType::Button:
        {
            if (isMouseOver && isLeftClick) {
                control.isPressed = true;
                if (control.onMouseBtnDown) control.onMouseBtnDown();
            }
            else if (isMouseOver && !isLeftClick) {
                control.isPressed = false;
                if (control.onMouseBtnUp) control.onMouseBtnUp();
            }
            break;
        }

        case GUIControlType::Scrollbar:
        {
            if (control.onMouseBtnDown) {
                control.onMouseBtnDown();
                control.isPressed = true;
            }
            int newPosition = mousePosition.y - control.position.y;
            UpdateScrollbar(newPosition);
            break;
        }

        default:
            break;
        }
    }
}

void GUIWindow::MoveWindow(const Vector2& newPosition, const std::unordered_map<std::string, std::shared_ptr<GUIWindow>>& allWindows) 
{
    WithDX11Renderer([this, newPosition, allWindows](std::shared_ptr<DX11Renderer> dx11Renderer) {
        const float screenWidth = dx11Renderer->iOrigWidth;
        const float screenHeight = dx11Renderer->iOrigHeight;

        Vector2 constrainedPosition = newPosition;

        // --- Snap to screen edges ---
        if (abs(constrainedPosition.x) <= SNAP_THRESHOLD) {
            constrainedPosition.x = 0.0f;
        }
        else if (abs(screenWidth - (constrainedPosition.x + size.x)) <= SNAP_THRESHOLD) {
            constrainedPosition.x = screenWidth - size.x;
        }

        if (abs(constrainedPosition.y) <= SNAP_THRESHOLD) {
            constrainedPosition.y = 0.0f;
        }
        else if (abs(screenHeight - (constrainedPosition.y + size.y)) <= SNAP_THRESHOLD) {
            constrainedPosition.y = screenHeight - size.y;
        }

        // --- Snap to peer windows ---
        for (const auto& [name, peer] : allWindows) {
            if (!peer || peer.get() == this || peer->bWindowDestroy || !peer->isVisible)
                continue;

            Vector2 peerPos = peer->position;
            Vector2 peerSize = peer->size;

            // Snap left edge to peer's right
            if (abs((constrainedPosition.x) - (peerPos.x + peerSize.x)) <= SNAP_THRESHOLD) {
                constrainedPosition.x = peerPos.x + peerSize.x;
            }
            // Snap right edge to peer's left
            else if (abs((constrainedPosition.x + size.x) - peerPos.x) <= SNAP_THRESHOLD) {
                constrainedPosition.x = peerPos.x - size.x;
            }

            // Snap top edge to peer's bottom
            if (abs((constrainedPosition.y) - (peerPos.y + peerSize.y)) <= SNAP_THRESHOLD) {
                constrainedPosition.y = peerPos.y + peerSize.y;
            }
            // Snap bottom edge to peer's top
            else if (abs((constrainedPosition.y + size.y) - peerPos.y) <= SNAP_THRESHOLD) {
                constrainedPosition.y = peerPos.y - size.y;
            }
        }

        // Clamp as fallback
        constrainedPosition.x = std::max(0.0f, std::min(constrainedPosition.x, screenWidth - size.x));
        constrainedPosition.y = std::max(0.0f, std::min(constrainedPosition.y, screenHeight - size.y));

        Vector2 delta = constrainedPosition - position;

        if (delta.x != 0 || delta.y != 0) {
            position = constrainedPosition;
            for (auto& control : controls) {
                control.position += delta;
            }
        }
        });


}

void GUIWindow::UpdateScrollbar(int newPosition) {
    scrollPosition = newPosition;
    if (scrollPosition < 0) scrollPosition = 0;
    if (scrollPosition > maxScrollPosition) scrollPosition = maxScrollPosition;

    // Calculate thumb size based on maxScrollPosition
    float thumbSize = 0.0f;
    if (maxScrollPosition > 0) {
        // Adjust thumb size based on the number of scrollable lines
        thumbSize = static_cast<float>(size.y) / (maxScrollPosition + 1);
    }
    else {
        // If no scrolling, thumb size matches the scrollbar size
        thumbSize = size.y;
    }

    // Ensure thumb size is within reasonable bounds
    thumbSize = std::max(thumbSize, 20.0f); // Minimum thumb size
    thumbSize = std::min(thumbSize, size.y); // Maximum thumb size

    // Fire scroll event
    for (auto& control : controls) {
        if (control.type == GUIControlType::Scrollbar && control.onScroll) {
            control.onScroll(scrollPosition);
        }
    }
}

// Calculate maxScrollPosition based on wrapped text height
void GUIWindow::CalculateScrollbarRange(float FontSize) {
    if (!myRenderer) return;
//    float textHeight = myRenderer->CalculateTextHeight(contentText, FontSize);
    maxScrollPosition = 0;
//    maxScrollPosition = static_cast<int>(textHeight - contentAreaSize.y);
    if (maxScrollPosition < 0) maxScrollPosition = 0; // Ensure it's not negative
}

// Helper function to wrap text
std::wstring GUIWindow::WrapText(const std::wstring& text, float maxWidth, float FontSize) {
    std::wstring wrappedText;
    std::wstring currentLine;
    float currentWidth = 0;

    for (wchar_t ch : text) {
        float charWidth = myRenderer->GetCharacterWidth(ch, FontSize);
        if (!currentLine.empty() || charWidth > maxWidth) 
        {
            wrappedText += currentLine + L"\n";
            currentLine.clear();
            currentWidth = 0;
        }
        currentLine += ch;
        currentWidth += charWidth;
    }

    if (!currentLine.empty()) {
        wrappedText += currentLine;
    }

    return wrappedText;
}

void GUIWindow::Render() {
    if ((!isVisible) || (!myRenderer)) return;

    // Render the background texture if it exists
    if (backgroundTextureId != -1) {
        myRenderer->DrawTexture(backgroundTextureId, position, size, MyColor(1.0f, 1.0f, 1.0f, 1.0f), true);
    }
    else {
        // Render the window background texture
        myRenderer->DrawRectangle(position, size, backgroundColor, true);
    }

    // Render each control
    for (auto& control : controls) {
        if (!control.isVisible) continue;

        MyColor bgColor = control.isHovered ? control.hoverColor : control.bgColor;

        switch (control.type) {
        case GUIControlType::Button: {
            if (control.bgTextureId != -1)
            {
                if (control.isHovered) {
                    myRenderer->DrawTexture(control.bgTextureHoverId, control.position, control.size, MyColor(255, 255, 255, 255), true);
                }
                else
                {
                    myRenderer->DrawTexture(control.bgTextureId, control.position, control.size, MyColor(128, 128, 128, 128), true);
                }

                // Calculate text position to center it both horizontally and vertically
                float textWidth = myRenderer->CalculateTextWidth(control.label, control.lblFontSize, float(control.size.x));
                float textHeight = myRenderer->CalculateTextHeight(control.label, control.lblFontSize, float(control.size.y));

                // Calculate the center position for the text
                float textX = (control.position.x + (control.size.x - textWidth) / control.lblFontSize) - textWidth / 2;
                float textY = (control.position.y + (control.size.y - textHeight) / control.lblFontSize) - 2;

                if (control.useShadowedText)
                {
                    // Draw the button shadow text
                    myRenderer->DrawMyTextCentered(control.label, Vector2(textX + 2.0f, textY + 2.0f), control.shadowedTxtColor, control.lblFontSize, control.size.x, control.size.y);
                }

                // Draw the main button text
                myRenderer->DrawMyTextCentered(control.label, Vector2(textX, textY), control.txtColor, control.lblFontSize, control.size.x, control.size.y);
                break;
            }
            else
            {
                // Draw the button background
                myRenderer->DrawRectangle(control.position, control.size, bgColor, true);

                // Calculate text position to center it both horizontally and vertically
                float textWidth = myRenderer->CalculateTextWidth(control.label, control.lblFontSize, control.size.x);
                float textHeight = myRenderer->CalculateTextHeight(control.label, control.lblFontSize, control.size.y);

                // Calculate the center position for the text
                float textX = control.position.x + (control.size.x - textWidth) / 2.0f;
                float textY = control.position.y + (control.size.y - textHeight) / 2.0f;

                if (control.useShadowedText)
                {
                    // Draw the button shadow text
                    myRenderer->DrawMyTextCentered(control.label, Vector2(textX + 2.0f, textY + 2.0f), control.shadowedTxtColor, control.lblFontSize, control.size.x, control.size.y);
                }

                // Draw the button text
                myRenderer->DrawMyTextCentered(control.label, Vector2(textX, textY), control.txtColor, control.lblFontSize, control.size.x, control.size.y);
            }
            break;
        }

        case GUIControlType::TextArea: {
            if (control.bgTextureId != -1)
            {
                if (control.isHovered) {
                    myRenderer->DrawTexture(control.bgTextureHoverId, control.position, control.size, MyColor(255, 255, 255, 255), true);
                }
                else
                {
                    myRenderer->DrawTexture(control.bgTextureId, control.position, control.size, MyColor(128, 128, 128, 255), true);
                }
            }
            else
            {
                // Draw the text area background using the control's color
                myRenderer->DrawRectangle(control.position, control.size, bgColor, true);

            }

            // Calculate offsets for text area.
            float textX = control.position.x + 5;
            float textY = control.position.y + 5;
            Vector2 resize = size;
            Vector2 resizedPos = Vector2(textX, textY);
            resize.x -= 12.0f;
            resize.y -= 6.0f;
            // Draw the text content with the specified text color
            myRenderer->DrawMyText(contentText, resizedPos, resize, control.txtColor, control.lblFontSize);

            break;
        }

        case GUIControlType::TitleBar: {
            if (control.bgTextureId != -1)
            {
                if (control.isHovered) {
                    myRenderer->DrawTexture(control.bgTextureHoverId, control.position, control.size, MyColor(255, 255, 255, 255), true);
                }
                else
                {
                    myRenderer->DrawTexture(control.bgTextureId, control.position, control.size, MyColor(64, 64, 64, 255), true);
                }

                // Calculate text position to center it both horizontally and vertically
                float textWidth = myRenderer->CalculateTextWidth(control.label, control.lblFontSize, control.size.x);
                float textHeight = myRenderer->CalculateTextHeight(control.label, control.lblFontSize, control.size.y);

                // Calculate the center position for the text
                float textX = control.position.x;
                float textY = (control.position.y + (control.size.y - textHeight) / 2.0f ) + 2;
                Vector2 recalcedPos = Vector2(textX, textY);
                // Draw the text content with the specified text color
                myRenderer->DrawMyText(control.label, recalcedPos, control.size, control.txtColor, control.lblFontSize);
            }
            else
            {
                // Draw the text area background using the control's color
                myRenderer->DrawRectangle(control.position, control.size, bgColor, true);

                // Draw the text content with the specified text color
                myRenderer->DrawMyText(control.label, control.position, control.size, control.txtColor, control.lblFontSize);
            }
            break;
        }
        case GUIControlType::Scrollbar: {
            // Draw the scrollbar background
            myRenderer->DrawRectangle(control.position, control.size, bgColor, true);
            break;
        }
        }
    }
}