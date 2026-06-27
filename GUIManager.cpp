/* ---------------------------------------------------------------------------------------------------------
Description: GUIManager.cpp

This file contains the implementation of the GUIManager class, which manages GUI windows, events and controls.

It includes methods for creating, removing, and rendering GUI windows, as well as handling input events.

Dependencies: Includes.h, Renderer.h, DX11Renderer.h, DX12Renderer.h, VulkanRenderer.h, OpenGLRenderer.h,
              GUIManager.h, SoundManager.h, Debug.h, GamePlayer.h

*/
#pragma once

#define NOMINMAN

#include "Includes.h"
#include "Renderer.h"

#if defined(_WIN32) || defined(_WIN64)
    #if defined(__USE_DIRECTX_11__)
        #include "DX11Renderer.h"
    #elif defined(__USE_DIRECTX_12__)
        #include "DX12Renderer.h"
    #elif defined(__USE_VULKAN__)
        #include "VULKAN_Renderer.h"
    #elif defined(__USE_OPENGL__)
        #include "OpenGLRenderer.h"
    #endif
#endif  // End of #if defined(_WIN32) || defined(_WIN64)

#include "GUIManager.h"
#include "SoundManager.h"
#include "Debug.h"

extern SoundManager soundManager;
extern HWND hwnd;
extern Debug debug;
extern ThreadManager threadManager;

GUIWindow::GUIWindow(const std::string& name, GUIWindowType type, const Vector2& position,
    const Vector2& size, const MyColor& backgroundColor, int backgroundTextureId, Renderer* renderer)
    : name(name), type(type), position(position), size(size), backgroundColor(backgroundColor),
    backgroundTextureId(backgroundTextureId), myRenderer(renderer)
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
    debug.logLevelMessage(LogLevel::LOG_INFO, L"GUIManager cleaned up.\n");
    bHasCleanedUp = true;
}

void GUIManager::Initialize(Renderer* renderer) {
    this->myRenderer = renderer;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Initializing GUIManager...\n");
}

bool GUIManager::IsClickCoolingDown() const {
    return std::chrono::steady_clock::now() < m_clickLockExpiry;
}

void GUIManager::AcquireClickLock() {
    m_clickLockExpiry = std::chrono::steady_clock::now() + kClickCooldown;
}

void GUIManager::CreateMyWindow(const std::string& name, GUIWindowType type, const Vector2& position, const Vector2& size,
    const MyColor& backgroundColor, int backgroundTextureId) {
    std::lock_guard<std::timed_mutex> lock(mutex);
    if (windows.find(name) != windows.end()) {
        debug.LogError("Window with name '" + name + "' already exists.\n");
        return;
    }

    // Create the window and pass the stored renderer
    std::shared_ptr<GUIWindow> window = std::make_shared<GUIWindow>(name, type, position, size, backgroundColor, backgroundTextureId, this->myRenderer);
    window->zOrder     = m_nextZOrder++;
    window->myRenderer = this->myRenderer;
    windows[name] = window;
}

void GUIManager::RemoveWindow(const std::string& name) {
    // Make a local copy of the name to prevent iterator invalidation
    std::string localWindowName = name;

    // Use debug output with proper string handling to prevent iterator issues
    debug.logDebugMessage(LogLevel::LOG_INFO, L"GUIManager::RemoveWindow - Attempting to remove window: %s",
        std::wstring(localWindowName.begin(), localWindowName.end()).c_str());

    // Use a timed lock so RemoveWindow doesn't silently no-op if the renderer
    // briefly holds the mutex during its snapshot phase.
    std::unique_lock<std::timed_mutex> lock(mutex, std::chrono::milliseconds(16));
    if (!lock.owns_lock()) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"GUIManager::RemoveWindow - Could not acquire mutex for window removal: %s",
            std::wstring(localWindowName.begin(), localWindowName.end()).c_str());
        return;
    }

    // Find the window with proper bounds checking using local copy
    auto it = windows.find(localWindowName);
    if (it == windows.end()) {
        // Log error for non-existent window with safe string handling
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"GUIManager::RemoveWindow - Window '%s' does not exist",
            std::wstring(localWindowName.begin(), localWindowName.end()).c_str());
        return;
    }

    // Verify window pointer is valid before accessing properties
    if (!it->second) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"GUIManager::RemoveWindow - Window '%s' has null pointer",
            std::wstring(localWindowName.begin(), localWindowName.end()).c_str());
        windows.erase(it); // Remove the invalid entry
        return;
    }

    // Check if window is already marked for destruction to prevent double-destruction
    if (it->second->bWindowDestroy) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"GUIManager::RemoveWindow - Window '%s' already marked for destruction",
            std::wstring(localWindowName.begin(), localWindowName.end()).c_str());
        return;
    }

    // Mark window for destruction first. This flag is checked by the render thread
    // and by HandleMouseClick after every callback, so they can bail out safely
    // without touching a half-destroyed vector.
    it->second->bWindowDestroy = true;

    // Null the renderer pointer so any in-flight Render() call that slips past the
    // bWindowDestroy check returns immediately on the !r guard.
    it->second->myRenderer = nullptr;

    // Clear all lambda callbacks on the live controls to break any circular
    // captures (e.g. [this] lambdas that reference the GUIManager). We do NOT
    // call controls.clear() here — doing so while the render thread or
    // HandleMouseClick may be iterating the same vector causes an access violation.
    // The vector is destroyed safely when the last shared_ptr to this window drops
    // (the render-thread snapshot holds one until end of frame).
    for (auto& control : it->second->controls) {
        control.onMouseBtnDown  = nullptr;
        control.onMouseBtnUp    = nullptr;
        control.onMouseOver     = nullptr;
        control.onMouseMove     = nullptr;
        control.onScroll        = nullptr;
        control.onSliderChanged = nullptr;
    }

    it->second->contentText.clear();

    // Remove from map — the shared_ptr ref-count drops but other holders
    // (render snapshot, the caller's stack) keep the object alive until they release.
    windows.erase(it);

    debug.logDebugMessage(LogLevel::LOG_INFO, L"GUIManager::RemoveWindow - Window '%s' successfully removed",
        std::wstring(localWindowName.begin(), localWindowName.end()).c_str());
}

void GUIManager::CloseAllWindows() {
    std::vector<std::string> names;
    {
        std::lock_guard<std::timed_mutex> lock(mutex);
        names.reserve(windows.size());
        for (const auto& [name, _] : windows)
            names.push_back(name);
    }
    for (const auto& name : names)
        RemoveWindow(name);
}

void GUIManager::OnWindowResize(int newWidth, int newHeight)
{
    auto menu = GetWindow("GameMenuWindow");
    if (!menu || menu->bWindowDestroy) return;

    // Move to correct right-edge position (screenWidth - menuWidth); MoveWindow applies delta to all controls.
    float newX = static_cast<float>(newWidth) - GAMEMENU_WINDOW_WIDTH;
    menu->MoveWindow(Vector2(newX, 0.0f), windows);

    // Update full-height panel to match new client height.
    menu->size.y = static_cast<float>(newHeight);
}

void GUIManager::Render() {
    if (!myRenderer) return;

    // Collect completed-fade callbacks to fire AFTER the mutex is released
    // so they can safely re-enter GUIManager methods without deadlocking.
    std::vector<std::function<void()>> completedCallbacks;

    // Build an owned-snapshot under the mutex so no concurrent RemoveWindow()
    // can free a window while we render it.  The lock is blocking (not try),
    // so the renderer thread waits the tiny fraction of a millisecond that
    // HandleAllInput or RemoveWindow may hold it — this eliminates the
    // starvation that try_to_lock caused when the renderer ran faster than
    // the message loop could win a single-shot race.
    std::vector<std::shared_ptr<GUIWindow>> snapshot;
    {
        std::lock_guard<std::timed_mutex> lock(mutex);

        // --- Tick all active window fades ---
        auto now = std::chrono::steady_clock::now();
        for (auto& [name, win] : windows) {
            if (!win || win->bWindowDestroy || !win->m_fade.active) continue;

            // Compute normalised progress [0, 1]
            float elapsed  = std::chrono::duration<float>(now - win->m_fade.startTime).count();
            float t        = (win->m_fade.duration > 0.0f)
                             ? std::clamp(elapsed / win->m_fade.duration, 0.0f, 1.0f)
                             : 1.0f;

            win->m_fadeAlpha = (win->m_fade.fadeType == GUIWindowFadeType::FadeIn)
                               ? t : (1.0f - t);

            if (t >= 1.0f) {
                // Fade finished
                win->m_fade.active = false;
                if (win->m_fade.fadeType == GUIWindowFadeType::FadeOut) {
                    // Hide the window and restore full alpha for the next show
                    win->isVisible   = false;
                    win->m_fadeAlpha = 1.0f;
                } else {
                    win->m_fadeAlpha = 1.0f;   // Ensure fully opaque after fade-in
                }
                // Queue callback for post-mutex dispatch
                if (win->m_fade.onComplete) {
                    completedCallbacks.push_back(std::move(win->m_fade.onComplete));
                    win->m_fade.onComplete = nullptr;
                }
            }
        }

        snapshot.reserve(windows.size());
        for (const auto& [name, window] : windows) {
            if (window && !window->bWindowDestroy && window->isVisible)
                snapshot.push_back(window);
        }
        // Render lowest zOrder first so the highest (topmost) window appears on top.
        std::sort(snapshot.begin(), snapshot.end(),
            [](const auto& a, const auto& b) { return a->zOrder < b->zOrder; });
    }  // mutex released before any rendering or callbacks

    // Fire completed-fade callbacks outside the mutex to prevent deadlocks
    for (auto& cb : completedCallbacks) {
        if (cb) cb();
    }

    for (const auto& window : snapshot) {
        if (!window->bWindowDestroy && window->isVisible)
            window->Render();
    }
}

void GUIManager::HandleAllInput(const Vector2& mousePosition, bool& isLeftClick) {
    // Use a timed lock: wait up to 8 ms for the mutex.  A plain try_to_lock
    // loses to the renderer thread (which holds this mutex briefly every frame)
    // causing permanent input starvation.  8 ms is short enough to keep
    // WndProc responsive but long enough to survive normal renderer contention.
    std::unique_lock<std::timed_mutex> lock(mutex, std::chrono::milliseconds(8));
    if (!lock.owns_lock()) {
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"GUIManager::HandleAllInput - Could not acquire mutex, skipping input handling");
        return;
    }

    // Create a snapshot of valid windows to avoid iterator invalidation
    std::vector<std::pair<std::string, std::shared_ptr<GUIWindow>>> validWindows;
    validWindows.reserve(windows.size());

    // Determine whether any modal window is currently active.
    // If so, only that window may receive input — all others are blocked.
    bool anyModal = false;
    for (const auto& windowPair : windows) {
        if (windowPair.second && !windowPair.second->bWindowDestroy &&
            windowPair.second->isVisible && windowPair.second->isModal) {
            anyModal = true;
            break;
        }
    }

    for (const auto& windowPair : windows) {
        if (!windowPair.second || windowPair.second->bWindowDestroy || !windowPair.second->isVisible)
            continue;
        if (anyModal && !windowPair.second->isModal)
            continue;
        validWindows.emplace_back(windowPair.first, windowPair.second);
    }

    // Sort topmost (highest zOrder) first so we can short-circuit input routing
    // as soon as the highest window in the stack claims the mouse position.
    std::sort(validWindows.begin(), validWindows.end(),
        [](const auto& a, const auto& b) { return a.second->zOrder > b.second->zOrder; });

    lock.unlock();

    if (validWindows.empty()) return;

    // --- Strict focused-window exclusive input model ---
    // The topmost (highest zOrder) visible window holds exclusive input focus.
    // ALL other windows receive NO input whatsoever — no hover, no clicks, no
    // callbacks.  Clicks outside the focused window's bounds are silently absorbed:
    // there is NO raise-to-front mechanism.  A window gains focus only when it is
    // created (CreateMyWindow assigns m_nextZOrder++) or explicitly brought to front
    // via BringWindowToFront() (e.g. ConsoleWindow::Toggle).  This guarantees that
    // no control on a background window can ever fire while a foreground window is
    // open, on ALL render pipelines.

    auto& focusedWin = *validWindows[0].second;

    bool mouseInFocused =
        mousePosition.x >= focusedWin.position.x &&
        mousePosition.x <= focusedWin.position.x + focusedWin.size.x &&
        mousePosition.y >= focusedWin.position.y &&
        mousePosition.y <= focusedWin.position.y + focusedWin.size.y;

    // Continue feeding mouse-move to the focused window during an in-progress drag
    // or slider interaction so the gesture does not freeze when the cursor leaves.
    bool hasActiveInteraction = focusedWin.isDragging;
    if (!hasActiveInteraction)
        for (const auto& ctrl : focusedWin.controls)
            if (ctrl.isPressed) { hasActiveInteraction = true; break; }

    if (mouseInFocused || hasActiveInteraction) {
        try {
            bool clickConsumed = false;
            focusedWin.HandleMouseClick(mousePosition, isLeftClick, this, clickConsumed);

            // Custom hit-test extension (e.g. console scrollbar) — called for clicks
            // in bounds that no registered GUIControl consumed.
            if (isLeftClick && mouseInFocused && focusedWin.onCustomMouseInput)
                focusedWin.onCustomMouseInput(mousePosition.x, mousePosition.y);

            focusedWin.HandleMouseMove(mousePosition, windows);
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"GUIManager::HandleAllInput - Exception in focused window '%s': %s",
                std::wstring(validWindows[0].first.begin(), validWindows[0].first.end()).c_str(),
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    } else {
        // Mouse is outside the focused window with no active interaction.
        // Clear hover and release stale states; the click is absorbed — nothing fires.
        for (auto& ctrl : focusedWin.controls)
            ctrl.isHovered = false;
        if (!isLeftClick) {
            focusedWin.isDragging = false;
            for (auto& ctrl : focusedWin.controls)
                ctrl.isPressed = false;
        }
    }

    // Background windows: strip all hover and release stale interactions.
    // They receive zero input — guaranteed.
    for (size_t i = 1; i < validWindows.size(); ++i) {
        if (!validWindows[i].second || validWindows[i].second->bWindowDestroy) continue;
        auto& bgWin = *validWindows[i].second;
        for (auto& ctrl : bgWin.controls)
            ctrl.isHovered = false;
        if (!isLeftClick) {
            bgWin.isDragging = false;
            for (auto& ctrl : bgWin.controls)
                ctrl.isPressed = false;
        }
    }
}

void GUIManager::HandleInput(const std::string& windowName, const Vector2& mousePosition, bool& isLeftClick) {
    std::shared_ptr<GUIWindow> window = GetWindow(windowName);
    if (!window || !window->isVisible || window->bWindowDestroy) return;

    bool clickConsumed = false;
    window->HandleMouseClick(mousePosition, isLeftClick, this, clickConsumed);
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

std::shared_ptr<GUIWindow> GUIManager::GetFocusedWindow() {
    std::unique_lock<std::timed_mutex> lock(mutex, std::chrono::milliseconds(8));
    if (!lock.owns_lock()) return nullptr;
    std::shared_ptr<GUIWindow> focused;
    int maxZ = -1;
    for (const auto& [name, win] : windows) {
        if (win && !win->bWindowDestroy && win->isVisible && win->zOrder > maxZ) {
            maxZ = win->zOrder;
            focused = win;
        }
    }
    return focused;
}

void GUIManager::BringWindowToFront(const std::string& name) {
    std::lock_guard<std::timed_mutex> lock(mutex);
    auto it = windows.find(name);
    if (it != windows.end() && it->second && !it->second->bWindowDestroy)
        it->second->zOrder = m_nextZOrder++;
}

void GUIManager::ApplyWindowFade(GUIWindowFadeType winfadeType, float overTimePeriod, const std::string& windowName) {
    ApplyWindowFadeCallback(winfadeType, overTimePeriod, windowName, nullptr);
}

void GUIManager::ApplyWindowFadeCallback(GUIWindowFadeType winfadeType, float overTimePeriod, const std::string& windowName, std::function<void()> callback) {
    std::lock_guard<std::timed_mutex> lock(mutex);
    auto it = windows.find(windowName);
    if (it == windows.end() || !it->second || it->second->bWindowDestroy) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"ApplyWindowFadeCallback - Window not found or already destroyed");
        return;
    }

    auto& win           = *it->second;
    win.m_fade.active   = true;
    win.m_fade.fadeType = winfadeType;
    win.m_fade.duration = overTimePeriod;
    win.m_fade.startTime= std::chrono::steady_clock::now();
    win.m_fade.onComplete = std::move(callback);

    if (winfadeType == GUIWindowFadeType::FadeIn) {
        // Start fully transparent and make window visible so it appears this frame
        win.m_fadeAlpha = 0.0f;
        win.isVisible   = true;
    } else {
        // Start fully opaque; the tick will hide the window once alpha reaches 0
        win.m_fadeAlpha = 1.0f;
        win.isVisible   = true;
    }
}

void GUIManager::HandleChar(wchar_t c) {
    auto focused = GetFocusedWindow();
    if (focused && focused->onCharInput)
        focused->onCharInput(c);
}

void GUIManager::HandleBackspace() {
    auto focused = GetFocusedWindow();
    if (!focused) return;
    for (auto& ctrl : focused->controls) {
        if (ctrl.type != GUIControlType::TextInput || !ctrl.isFocused) continue;
        if (ctrl.cursorPos > 0 && !ctrl.inputText.empty()) {
            ctrl.inputText.erase(ctrl.cursorPos - 1, 1);
            --ctrl.cursorPos;
            if (ctrl.onTextChanged) ctrl.onTextChanged(ctrl.inputText);
        }
        return;
    }
    if (focused->onBackspace)
        focused->onBackspace();
}

void GUIManager::HandleDelete() {
    auto focused = GetFocusedWindow();
    if (!focused) return;
    for (auto& ctrl : focused->controls) {
        if (ctrl.type != GUIControlType::TextInput || !ctrl.isFocused) continue;
        if (ctrl.cursorPos < static_cast<int>(ctrl.inputText.size())) {
            ctrl.inputText.erase(ctrl.cursorPos, 1);
            if (ctrl.onTextChanged) ctrl.onTextChanged(ctrl.inputText);
        }
        return;
    }
}

void GUIManager::HandleEnter() {
    auto focused = GetFocusedWindow();
    if (focused && focused->onEnter)
        focused->onEnter();
}

void GUIManager::HandleMouseWheel(int delta) {
    auto focused = GetFocusedWindow();
    if (!focused) return;

    // If the focused window has registered its own wheel handler, use it.
    if (focused->onMouseWheel) {
        focused->onMouseWheel(delta);
        return;
    }

    // Otherwise try to scroll the first visible ListBox in that window.
    // delta is positive = scroll up (decrease offset), negative = scroll down.
    for (auto& ctrl : focused->controls) {
        if (ctrl.type != GUIControlType::ListBox || !ctrl.isVisible) continue;
        const int iH    = ctrl.listItemHeight > 0 ? ctrl.listItemHeight : 22;
        const int vis   = static_cast<int>(ctrl.size.y / static_cast<float>(iH));
        const int total = static_cast<int>(ctrl.items.size());
        int step        = (delta > 0) ? -3 : 3;          // 3-row scroll per tick
        ctrl.listScrollOffset = std::clamp(
            ctrl.listScrollOffset + step, 0, std::max(0, total - vis));
        break;                                             // only scroll the first ListBox
    }
}

void GUIWindow::HandleMouseMove(const Vector2& mousePosition, const std::unordered_map<std::string, std::shared_ptr<GUIWindow>>& allWindows) {
    if (bWindowDestroy || !isVisible) return;
    if (m_fade.active) return;

    for (auto& control : controls) {
        if (bWindowDestroy) break;
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

            case GUIControlType::HSlider:
            {
                if (control.isVisible && control.isPressed) {
                    const float knobW  = 14.0f;
                    float usableW = control.size.x - knobW;
                    if (usableW > 0.0f) {
                        float t = std::clamp(
                            (mousePosition.x - control.position.x - knobW * 0.5f) / usableW,
                            0.0f, 1.0f);
                        control.sliderValue = control.sliderMin + t * (control.sliderMax - control.sliderMin);
                        if (control.onSliderChanged) control.onSliderChanged(control.sliderValue);
                    }
                }
                break;
            }

            case GUIControlType::Scrollbar:
            {
                if (control.isPressed) {
                    int newPosition = static_cast<int>(mousePosition.y - control.position.y);
                    UpdateScrollbar(newPosition);
                }
                break;
            }

            case GUIControlType::ToggleSlider:
                break;  // click-only control, no drag logic

            case GUIControlType::Panel:
                break;  // decorative only

            case GUIControlType::TextInput:
                break;  // cursor placement handled in HandleMouseClick

            case GUIControlType::ListBox:
            {
                // Continue scrollbar thumb drag while button is held
                if (control.isPressed && control.isActive) {
                    const int   total = static_cast<int>(control.items.size());
                    const int   vis   = static_cast<int>(control.size.y /
                        static_cast<float>(control.listItemHeight > 0 ? control.listItemHeight : 22));
                    float maxScr  = static_cast<float>(total - vis);
                    if (maxScr > 0.0f) {
                        float delta   = mousePosition.y - control.sliderValue;
                        control.sliderValue = mousePosition.y;
                        float trackH  = control.size.y - 2.0f;
                        float iHf     = static_cast<float>(control.listItemHeight > 0 ? control.listItemHeight : 22);
                        float thumbH  = std::max(20.0f, trackH * (static_cast<float>(vis) / static_cast<float>(total)));
                        float step    = (maxScr / std::max(1.0f, trackH - thumbH)) * delta;
                        float newOff  = std::clamp(
                            static_cast<float>(control.listScrollOffset) + step,
                            0.0f, maxScr);
                        control.listScrollOffset = static_cast<int>(std::round(newOff));
                    }
                }
                break;
            }

            case GUIControlType::ComboBox:
                break;  // dropdown hover tracking done in click handler

            default:
                break;
        }
    }
}

void GUIWindow::HandleMouseClick(const Vector2& mousePosition, bool& isLeftClick, GUIManager* guiMgr, bool& clickConsumed) {
    if (bWindowDestroy) return;
    if (m_fade.active) return;

    // Pre-pass: close any open ComboBox whose click lands outside both its main
    // rect AND its open dropdown panel so the dropdown dismisses properly.
    if (isLeftClick) {
        for (auto& ctrl : controls) {
            if (ctrl.type != GUIControlType::ComboBox || !ctrl.isDropdownOpen) continue;
            // Main box rect
            bool inMain = (mousePosition.x >= ctrl.position.x &&
                           mousePosition.x <= ctrl.position.x + ctrl.size.x &&
                           mousePosition.y >= ctrl.position.y &&
                           mousePosition.y <= ctrl.position.y + ctrl.size.y);
            // Dropdown rect (below the control)
            const int   total    = static_cast<int>(ctrl.items.size());
            const int   maxRows  = std::min(ctrl.dropdownMaxRows, total);
            const float iH       = static_cast<float>(ctrl.listItemHeight > 0 ? ctrl.listItemHeight : 22);
            const float dropH    = static_cast<float>(maxRows) * iH + 4.0f;
            float dropY          = ctrl.position.y + ctrl.size.y;
            bool inDrop = (mousePosition.x >= ctrl.position.x &&
                           mousePosition.x <= ctrl.position.x + ctrl.size.x &&
                           mousePosition.y >= dropY &&
                           mousePosition.y <= dropY + dropH);
            if (!inMain && !inDrop) {
                ctrl.isDropdownOpen = false;
            }
        }
    }

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
                if (!control.isPressed) {
                    // Guard: skip if another control already claimed this click this frame,
                    // or if the 1-second cross-frame cooldown is still active.
                    if (!clickConsumed && (!guiMgr || !guiMgr->IsClickCoolingDown())) {
                        control.isPressed = true;
                        clickConsumed = true;
                        if (guiMgr) guiMgr->AcquireClickLock();
                        if (control.onMouseBtnDown) control.onMouseBtnDown();
                        // The callback may have called RemoveWindow on this window
                        // (e.g. a close button). If so, stop iterating immediately —
                        // continuing would access the now-destroyed controls vector.
                        if (bWindowDestroy) return;
                    }
                }
            }
            else if (isMouseOver && !isLeftClick && control.isPressed) {
                // Button released while still over the control — completed click.
                // Up always fires for the control that owns isPressed; also marks consumed
                // so other windows cannot simultaneously claim the release.
                control.isPressed = false;
                clickConsumed = true;
                if (control.onMouseBtnUp) control.onMouseBtnUp();
                if (bWindowDestroy) return;
            }
            else if (!isLeftClick) {
                // Released outside the control — cancel press without firing
                control.isPressed = false;
            }
            break;
        }

        case GUIControlType::Scrollbar:
        {
            if (isLeftClick) {
                if (isMouseOver && !control.isPressed && !clickConsumed && (!guiMgr || !guiMgr->IsClickCoolingDown())) {
                    control.isPressed = true;
                    clickConsumed = true;
                    if (guiMgr) guiMgr->AcquireClickLock();
                    SetCapture(hwnd);
                    if (control.onMouseBtnDown) control.onMouseBtnDown();
                }
                if (control.isPressed) {
                    int newPosition = static_cast<int>(mousePosition.y - control.position.y);
                    UpdateScrollbar(newPosition);
                }
            } else {
                if (control.isPressed) {
                    control.isPressed = false;
                    ReleaseCapture();
                    if (control.onMouseBtnUp) control.onMouseBtnUp();
                }
            }
            break;
        }

        case GUIControlType::HSlider:
        {
            if (!control.isVisible) break;
            if (isMouseOver && isLeftClick) {
                if (!control.isPressed) {
                    control.isPressed = true;
                    control.isActive  = true;
                    // Deactivate every other slider on this window
                    for (auto& other : controls)
                        if (&other != &control && other.type == GUIControlType::HSlider)
                            other.isActive = false;
                }
                const float knobW  = 14.0f;
                float usableW = control.size.x - knobW;
                if (usableW > 0.0f) {
                    float t = std::clamp(
                        (mousePosition.x - control.position.x - knobW * 0.5f) / usableW,
                        0.0f, 1.0f);
                    control.sliderValue = control.sliderMin + t * (control.sliderMax - control.sliderMin);
                    if (control.onSliderChanged) control.onSliderChanged(control.sliderValue);
                }
            }
            else if (!isLeftClick) {
                control.isPressed = false;
            }
            break;
        }

        case GUIControlType::ToggleSlider:
        {
            if (!control.isVisible) break;
            if (isMouseOver && isLeftClick) {
                if (!control.isPressed) {
                    control.isPressed   = true;
                    control.sliderValue = (control.sliderValue >= 0.5f) ? 0.0f : 1.0f;
                    if (control.onSliderChanged) control.onSliderChanged(control.sliderValue);
                }
            }
            else if (!isLeftClick) {
                control.isPressed = false;
            }
            break;
        }

        // Panel controls are purely decorative — no click handling needed.
        case GUIControlType::Panel:
            break;

        case GUIControlType::TextInput:
        {
            if (!control.isVisible) break;
            if (isMouseOver && isLeftClick) {
                if (!control.isPressed && !clickConsumed && (!guiMgr || !guiMgr->IsClickCoolingDown())) {
                    control.isPressed   = true;
                    clickConsumed       = true;
                    if (guiMgr) guiMgr->AcquireClickLock();

                    // Give keyboard focus to this TextInput; strip it from every other
                    for (auto& other : controls)
                        if (&other != &control && other.type == GUIControlType::TextInput)
                            other.isFocused = false;
                    control.isFocused = true;

                    // Position cursor at the click point
                    const float fs     = control.lblFontSize > 0.0f ? control.lblFontSize : 12.0f;
                    const float textX  = control.position.x + 6.0f;
                    float runX         = textX;
                    int   newCursor    = 0;
                    for (int ci = 0; ci < (int)control.inputText.size(); ++ci) {
                        float cw = myRenderer ? myRenderer->GetCharacterWidth(control.inputText[ci], fs, control.bold) : fs * 0.6f;
                        if (mousePosition.x < runX + cw * 0.5f) break;
                        runX += cw;
                        newCursor = ci + 1;
                    }
                    control.cursorPos = newCursor;
                }
            }
            else if (!isLeftClick) {
                control.isPressed = false;
            }
            break;
        }

        case GUIControlType::ListBox:
        {
            if (!control.isVisible) break;
            const float sbW  = 12.0f;
            const float iH   = static_cast<float>(control.listItemHeight > 0 ? control.listItemHeight : 22);
            const float sbX  = control.position.x + control.size.x - sbW - 1.0f;
            const int   total= static_cast<int>(control.items.size());
            const int   vis  = static_cast<int>(control.size.y / iH);

            bool inScrollbar = (isMouseOver &&
                                mousePosition.x >= sbX &&
                                total > vis);

            if (isLeftClick) {
                if (isMouseOver && !clickConsumed && (!guiMgr || !guiMgr->IsClickCoolingDown())) {
                    if (!control.isPressed) {
                        control.isPressed = true;
                        clickConsumed     = true;
                        if (guiMgr) guiMgr->AcquireClickLock();

                        if (inScrollbar) {
                            // Start scrollbar thumb drag
                            control.isActive    = true;
                            control.sliderValue = mousePosition.y;  // drag anchor Y
                            SetCapture(hwnd);
                        } else {
                            // Item selection
                            control.isActive = false;
                            int clicked = control.listScrollOffset +
                                          static_cast<int>((mousePosition.y - control.position.y) / iH);
                            if (clicked >= 0 && clicked < total) {
                                control.selectedIndex = clicked;
                                if (control.onSelectionChanged)
                                    control.onSelectionChanged(control.selectedIndex);
                            }
                        }
                    } else if (control.isActive) {
                        // Continue scrollbar drag in the same press
                        float delta    = mousePosition.y - control.sliderValue;
                        control.sliderValue = mousePosition.y;
                        float maxScr   = static_cast<float>(total - vis);
                        if (maxScr > 0.0f) {
                            float trackH  = control.size.y - 2.0f;
                            float thumbH  = std::max(20.0f, trackH * (float(vis) / float(total)));
                            float step    = (maxScr / std::max(1.0f, trackH - thumbH)) * delta;
                            float newOff  = std::clamp(
                                static_cast<float>(control.listScrollOffset) + step,
                                0.0f, maxScr);
                            control.listScrollOffset = static_cast<int>(std::round(newOff));
                        }
                    }
                }
            } else {
                if (control.isPressed) {
                    control.isPressed = false;
                    if (control.isActive) {
                        control.isActive = false;
                        ReleaseCapture();
                    }
                }
            }
            break;
        }

        case GUIControlType::ComboBox:
        {
            if (!control.isVisible) break;
            if (isMouseOver && isLeftClick) {
                if (!control.isPressed && !clickConsumed && (!guiMgr || !guiMgr->IsClickCoolingDown())) {
                    control.isPressed = true;
                    clickConsumed     = true;
                    if (guiMgr) guiMgr->AcquireClickLock();

                    if (!control.isDropdownOpen) {
                        // Open the dropdown
                        control.isDropdownOpen = true;
                    } else {
                        // Dropdown is open — check if click is inside the dropdown panel
                        const int   total   = static_cast<int>(control.items.size());
                        const int   maxRows = std::min(control.dropdownMaxRows, total);
                        const float iH      = static_cast<float>(control.listItemHeight > 0 ? control.listItemHeight : 22);
                        float dropY         = control.position.y + control.size.y;

                        if (mousePosition.y >= dropY &&
                            mousePosition.y <  dropY + float(maxRows) * iH + 4.0f) {
                            // Clicked inside the open dropdown list
                            int row = static_cast<int>((mousePosition.y - dropY - 2.0f) / iH);
                            if (row >= 0 && row < total) {
                                control.selectedIndex = row;
                                if (control.onSelectionChanged)
                                    control.onSelectionChanged(control.selectedIndex);
                            }
                        }
                        control.isDropdownOpen = false;
                    }
                }
            }
            else if (!isLeftClick) {
                control.isPressed = false;
            }
            break;
        }

        default:
            break;
        }
    }
}

void GUIWindow::MoveWindow(const Vector2& newPosition, const std::unordered_map<std::string, std::shared_ptr<GUIWindow>>& allWindows) 
{
    const float screenWidth = renderer->iOrigWidth;
    const float screenHeight = renderer->iOrigHeight;

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
    // Snapshot myRenderer into a local so a concurrent RemoveWindow() nulling
    // the member cannot cause a null-ptr crash mid-frame.
    Renderer* r = myRenderer;
    if (!isVisible || !r || bWindowDestroy) return;
    if (!r->bIsInitialized.load() ||
        threadManager.threadVars.bIsShuttingDown.load() ||
        threadManager.threadVars.bSettingFullScreen.load() ||
        threadManager.threadVars.bIsResizing.load()) return;

    // Fade alpha helper — scales a colour's alpha channel by the current window
    // opacity so all draw calls honour the fade state across all render pipelines.
    auto fc = [this](MyColor c) -> MyColor {
        c.a = static_cast<uint8_t>(c.a * m_fadeAlpha);
        return c;
    };

    // Pre-render hook — draw drop shadows or other geometry that must appear
    // behind the window background and all controls.
    if (onPreRender) {
        try { onPreRender(r); }
        catch (...) {}
    }

    // Render the background texture if it exists
    if (backgroundTextureId != -1) {
        r->DrawTexture(backgroundTextureId, position, size,
                       fc(MyColor(255, 255, 255, 255)), true);
    }
    else {
        // Render the window background
        r->DrawRectangle(position, size, fc(backgroundColor), true);
    }

    // Render each control.
    // Controls marked clipContent=true are wrapped in a renderer scissor rect so that
    // content overflowing the bevel box (e.g. scrolled tab controls) is pixel-accurately
    // clipped.  The clip is pushed on the first visible clipContent control and popped
    // when the first subsequent visible non-clipContent control is hit (or at loop end),
    // so title bar, tab buttons, and bottom buttons are never scissored.
    bool clipActive = false;
    for (auto& control : controls) {
        // The render thread holds a snapshot shared_ptr so the window object stays
        // alive, but RemoveWindow can set bWindowDestroy while we are mid-loop.
        // Check each iteration so we never touch a control whose callbacks were
        // already nulled by RemoveWindow.
        if (bWindowDestroy) break;
        if (!control.isVisible) continue;

        // Push clip rect the first time we hit a clipContent control
        if (control.clipContent && !clipActive && m_hasClip) {
            r->PushClipRect(m_clipPos.x, m_clipPos.y, m_clipSize.x, m_clipSize.y);
            clipActive = true;
        }
        // Pop clip rect when we leave the clipContent block
        else if (!control.clipContent && clipActive) {
            r->PopClipRect();
            clipActive = false;
        }

        MyColor bgColor = control.isHovered ? control.hoverColor : control.bgColor;

        switch (control.type) {
            case GUIControlType::Button: {
                if (control.useCircleShape) {
                    // Render button as a 3D filled circle with drop shadow
                    float cx = control.position.x + control.size.x * 0.5f;
                    float cy = control.position.y + control.size.y * 0.5f;
                    float radius = std::min(control.size.x, control.size.y) * 0.5f;
                    MyColor circleColor = control.isHovered ? control.hoverColor : control.bgColor;
                    // Drop shadow: slightly larger, offset down-right, semi-transparent black
                    r->DrawCircle(Vector2(cx + 2.5f, cy + 2.5f), radius + 1.0f,
                        fc(MyColor(0, 0, 0, 120)), true);
                    // Main circle fill
                    r->DrawCircle(Vector2(cx, cy), radius, fc(circleColor), true);
                    // Dark outline ring for definition
                    r->DrawCircle(Vector2(cx, cy), radius, fc(MyColor(0, 0, 0, 160)), false);
                    // 3D top-left shine: small bright highlight arc
                    r->DrawCircle(Vector2(cx - radius * 0.22f, cy - radius * 0.28f),
                        radius * 0.42f, fc(MyColor(255, 255, 255, 80)), true);
                    if (control.useShadowedText) {
                        r->DrawMyTextCentered(control.label,
                            Vector2(control.position.x + 1.0f, control.position.y + 1.0f),
                            fc(control.shadowedTxtColor), control.lblFontSize, control.size.x, control.size.y, control.bold);
                    }
                    r->DrawMyTextCentered(control.label, control.position,
                        fc(control.txtColor), control.lblFontSize, control.size.x, control.size.y, control.bold);
                    break;
                }
                if (control.bgTextureId != -1)
                {
                    if (control.isHovered) {
                        // Hover: always fully opaque regardless of bgColor.a
                        r->DrawTexture(control.bgTextureHoverId, control.position, control.size, fc(MyColor(255, 255, 255, 255)), true);
                    }
                    else
                    {
                        // Default: use bgColor.a as the alpha tint so semi-transparent
                        // buttons (bgColor.a < 255) render at the correct opacity.
                        r->DrawTexture(control.bgTextureId, control.position, control.size,
                                       fc(MyColor(255, 255, 255, control.bgColor.a)), true);
                    }

                    // Pass the button's top-left corner and dimensions; DrawMyTextCentered
                    // measures the text itself and computes the true centre position.
                    if (control.useShadowedText)
                    {
                        r->DrawMyTextCentered(control.label,
                            Vector2(control.position.x + 2.0f, control.position.y + 2.0f),
                            fc(control.shadowedTxtColor), control.lblFontSize, control.size.x, control.size.y, control.bold);
                    }
                    r->DrawMyTextCentered(control.label, control.position,
                        fc(control.txtColor), control.lblFontSize, control.size.x, control.size.y, control.bold);
                    break;
                }
                else
                {
                    // Draw the button background
                    r->DrawRectangle(control.position, control.size, fc(bgColor), true);

                    if (control.useShadowedText)
                    {
                        r->DrawMyTextCentered(control.label,
                            Vector2(control.position.x + 2.0f, control.position.y + 2.0f),
                            fc(control.shadowedTxtColor), control.lblFontSize, control.size.x, control.size.y, control.bold);
                    }
                    r->DrawMyTextCentered(control.label, control.position,
                        fc(control.txtColor), control.lblFontSize, control.size.x, control.size.y, control.bold);
                }
                break;
            }

            case GUIControlType::TextArea: {
                if (control.bgTextureId != -1)
                {
                    if (control.isHovered) {
                        r->DrawTexture(control.bgTextureHoverId, control.position, control.size, fc(MyColor(255, 255, 255, 255)), true);
                    }
                    else
                    {
                        r->DrawTexture(control.bgTextureId, control.position, control.size, fc(MyColor(128, 128, 128, 255)), true);
                    }
                }
                else
                {
                    // Draw the text area background using the control's color
                    r->DrawRectangle(control.position, control.size, fc(bgColor), true);

                }

                // Calculate offsets for text area.
                float textX = control.position.x + 5;
                float textY = control.position.y + 5;
                // Use the control's own size (not the window size) so text starts
                // at the control's left edge and is clipped to the control's width.
                Vector2 resize = control.size;
                Vector2 resizedPos = Vector2(textX, textY);
                resize.x -= 12.0f;
                resize.y -= 6.0f;
                // Optional drop-shadow (drawn 1px offset before main text)
                if (control.useShadowedText) {
                    r->DrawMyText(control.label,
                        Vector2(resizedPos.x + 1.0f, resizedPos.y + 1.0f),
                        resize, fc(control.shadowedTxtColor), control.lblFontSize);
                }
                // Draw the text content with the specified text color
                r->DrawMyText(control.label, resizedPos, resize, fc(control.txtColor), control.lblFontSize);

                break;
            }

            case GUIControlType::TitleBar: {
                if (control.useGradient) {
                    constexpr int NUM_BANDS = 15;
                    for (int bi = 0; bi < NUM_BANDS; ++bi) {
                        float frac = static_cast<float>(bi) / static_cast<float>(NUM_BANDS - 1);
                        MyColor band;
                        band.r = static_cast<uint8_t>(control.gradientTopColor.r + (control.gradientBottomColor.r - control.gradientTopColor.r) * frac);
                        band.g = static_cast<uint8_t>(control.gradientTopColor.g + (control.gradientBottomColor.g - control.gradientTopColor.g) * frac);
                        band.b = static_cast<uint8_t>(control.gradientTopColor.b + (control.gradientBottomColor.b - control.gradientTopColor.b) * frac);
                        band.a = 255;
                        float bandH = std::ceil(control.size.y / NUM_BANDS) + 1.0f;
                        float bandY = control.position.y + (control.size.y / NUM_BANDS) * bi;
                        r->DrawRectangle(Vector2(control.position.x, bandY),
                                         Vector2(control.size.x, bandH),
                                         fc(band), true);
                    }
                }
                else if (control.bgTextureId != -1)
                {
                    if (control.isHovered) {
                        r->DrawTexture(control.bgTextureHoverId, control.position, control.size, fc(MyColor(255, 255, 255, 255)), true);
                    }
                    else
                    {
                        r->DrawTexture(control.bgTextureId, control.position, control.size, fc(MyColor(200, 200, 200, 255)), true);
                    }
                }
                else
                {
                    r->DrawRectangle(control.position, control.size, fc(bgColor), true);
                }
                // Caption vertical centring: startY = barTop + (barHeight - captionHeight) / 2
                if (!control.label.empty())
                {
                    const float captionH = control.lblFontSize * 1.25f;
                    const float captionY = control.position.y + (control.size.y - captionH) * 0.5f;

                    if (control.lblCenterH)
                    {
                        float captionW = 0.0f;
                        for (wchar_t ch : control.label)
                            captionW += r->GetCharacterWidth(ch, control.lblFontSize);

                        const float captionX = control.position.x +
                            (control.size.x - captionW) * 0.5f;
                        r->DrawMyText(control.label,
                                      Vector2(captionX, captionY),
                                      fc(control.txtColor), control.lblFontSize);
                    }
                    else
                    {
                        r->DrawMyText(control.label,
                                      Vector2(control.position.x + 6.0f, captionY),
                                      fc(control.txtColor), control.lblFontSize);
                    }
                }
                break;
            }
            case GUIControlType::Scrollbar: {
                // Draw the scrollbar background
                r->DrawRectangle(control.position, control.size, fc(bgColor), true);
                break;
            }

            case GUIControlType::ToggleSlider: {
                const bool  on    = (control.sliderValue >= 0.5f);
                const float knobH = control.size.y - 6.0f;
                const float knobW = knobH;                              // Equal width = circular knob
                const float knobY = control.position.y + 3.0f;

                // Track — pill / capsule shape. Keep the track fully opaque so
                // antialiased cap pixels do not blend into visible circular bands.
                const MyColor trackBg = on
                    ? (control.isHovered ? MyColor(34, 172, 56, 255) : MyColor(24, 138, 42, 255))
                    : (control.isHovered ? MyColor(78, 80, 88, 255) : MyColor(54, 56, 64, 255));

                // Pill geometry: draw middle rect base first, then circles overdraw for clean rounded caps
                const float pillR  = control.size.y * 0.5f;
                const float pillCY = control.position.y + pillR;
                const float pillLX = control.position.x + pillR;       // left cap centre X
                const float pillRX = control.position.x + control.size.x - pillR; // right cap X
                // Base: middle fill rectangle drawn first so cap circles cleanly overdraw its corners
                r->DrawRectangle(Vector2(pillLX, control.position.y),
                    Vector2(pillRX - pillLX, control.size.y), fc(trackBg), true);
                // Left rounded cap — drawn on top to cover rect corner artifacts
                r->DrawCircle(Vector2(pillLX, pillCY), pillR, fc(trackBg), true);
                // Right rounded cap — drawn on top to cover rect corner artifacts
                r->DrawCircle(Vector2(pillRX, pillCY), pillR, fc(trackBg), true);

                // Knob x position (left = OFF, right = ON)
                float knobX = on
                    ? control.position.x + control.size.x - knobW - 2.0f
                    : control.position.x + 2.0f;

                // Circular knob — centre point for DrawCircle
                const float knobR  = knobW * 0.5f;
                const float knobCX = knobX + knobR;
                const float knobCY = knobY + knobR;

                // Knob drop-shadow circle
                r->DrawCircle(Vector2(knobCX + 1.5f, knobCY + 1.5f), knobR,
                    fc(MyColor(0, 0, 0, 110)), true);

                // Knob body — same beveled drawing for both states. The ON state
                // receives a warmer green-tinted gradient instead of a flat white disc.
                MyColor knobFill = on
                    ? MyColor(202, 230, 206, 255) : MyColor(182, 186, 200, 255);
                r->DrawCircle(Vector2(knobCX, knobCY), knobR, fc(knobFill), true);

                // Soft radial-style layering keeps the knob readable on both
                // green and grey tracks without allocating textures.
                r->DrawCircle(Vector2(knobCX - knobR * 0.12f, knobCY - knobR * 0.18f), knobR * 0.72f,
                    fc(on ? MyColor(238, 255, 240, 115) : MyColor(245, 247, 255, 92)), true);
                r->DrawCircle(Vector2(knobCX + knobR * 0.16f, knobCY + knobR * 0.18f), knobR * 0.56f,
                    fc(on ? MyColor(105, 160, 112, 52) : MyColor(92, 98, 118, 58)), true);

                // Outer ring bevel — lighter top-left, darker bottom-right
                {
                    MyColor ringHi = on ? MyColor(250, 255, 250, 170) : MyColor(218, 222, 234, 180);
                    // Top-left arc approximated with a slightly offset unfilled circle
                    r->DrawCircle(Vector2(knobCX, knobCY), knobR, fc(ringHi), false);
                }

                // Knob top glass shine (small bright circle near top)
                r->DrawCircle(Vector2(knobCX, knobCY - knobR * 0.3f), knobR * 0.28f,
                    fc(MyColor(255, 255, 255, on ? 110 : 85)), true);

                break;
            }

            case GUIControlType::HSlider: {
                if (control.drawAsPill) {
                    // Stat-bar pill gauge — slim 3-D green horizontal pill
                    const float trough_margin = 3.0f;
                    const float pillH = std::floor(control.size.y * 0.54f);
                    const float pillY = control.position.y + (control.size.y - pillH) * 0.5f;
                    const float pillTotalW = control.size.x - trough_margin * 2.0f;

                    float t = (control.sliderMax > control.sliderMin)
                        ? std::clamp((control.sliderValue - control.sliderMin)
                                     / (control.sliderMax - control.sliderMin), 0.0f, 1.0f)
                        : 0.0f;
                    float fillW = pillTotalW * t;

                    // Outer dark trough
                    r->DrawRectangle(control.position, control.size, fc(MyColor(10, 12, 10, 200)), true);
                    // Sunken trough inner
                    r->DrawRectangle(Vector2(control.position.x + trough_margin, pillY),
                        Vector2(pillTotalW, pillH), fc(MyColor(6, 8, 6, 255)), true);
                    r->DrawRectangle(Vector2(control.position.x + trough_margin, pillY),
                        Vector2(pillTotalW, 1.0f), fc(MyColor(3, 4, 3, 255)), true);
                    r->DrawRectangle(Vector2(control.position.x + trough_margin, pillY),
                        Vector2(1.0f, pillH), fc(MyColor(3, 4, 3, 255)), true);
                    r->DrawRectangle(Vector2(control.position.x + trough_margin, pillY + pillH - 1.0f),
                        Vector2(pillTotalW, 1.0f), fc(MyColor(50, 80, 50, 160)), true);

                    if (fillW > 1.5f) {
                        // Fine multi-band gradient from bright top to dark bottom (like titlebar)
                        constexpr int PILL_BANDS = 8;
                        MyColor topC(
                            static_cast<uint8_t>(std::min(255, (int)(control.pillFillColor.r * 1.55f))),
                            static_cast<uint8_t>(std::min(255, (int)(control.pillFillColor.g * 1.55f))),
                            static_cast<uint8_t>(std::min(255, (int)(control.pillFillColor.b * 1.55f))),
                            control.pillFillColor.a);
                        MyColor botC(
                            static_cast<uint8_t>(control.pillFillColor.r * 0.22f),
                            static_cast<uint8_t>(control.pillFillColor.g * 0.22f),
                            static_cast<uint8_t>(control.pillFillColor.b * 0.22f),
                            control.pillFillColor.a);
                        for (int bi = 0; bi < PILL_BANDS; ++bi) {
                            float frac = static_cast<float>(bi) / static_cast<float>(PILL_BANDS - 1);
                            MyColor band(
                                static_cast<uint8_t>(topC.r + (botC.r - topC.r) * frac),
                                static_cast<uint8_t>(topC.g + (botC.g - topC.g) * frac),
                                static_cast<uint8_t>(topC.b + (botC.b - topC.b) * frac),
                                control.pillFillColor.a);
                            float bandH = std::ceil(pillH / PILL_BANDS) + 1.0f;
                            float bandY = pillY + (pillH / PILL_BANDS) * bi;
                            r->DrawRectangle(
                                Vector2(control.position.x + trough_margin, bandY),
                                Vector2(fillW, bandH), fc(band), true);
                        }
                        // Top highlight streak
                        r->DrawRectangle(
                            Vector2(control.position.x + trough_margin, pillY),
                            Vector2(fillW, 1.0f),
                            fc(MyColor(210, 255, 210, 190)), true);
                        // Bottom shadow streak
                        r->DrawRectangle(
                            Vector2(control.position.x + trough_margin, pillY + pillH - 1.0f),
                            Vector2(fillW, 1.0f),
                            fc(MyColor(0, 50, 0, 220)), true);
                    }
                    break;
                }

                const float knobW  = 16.0f;
                const float trackH = 8.0f;

                // Outer area dark fill (backdrop for the whole slider)
                r->DrawRectangle(control.position, control.size,
                    fc(MyColor(8, 9, 16, 210)), true);

                // Track groove — 3D sunken bevel channel
                float trackY = control.position.y + (control.size.y - trackH) * 0.5f;
                float trackStartX = control.position.x + knobW * 0.5f;
                float trackUsableW = control.size.x - knobW;

                // Outer sunken bevel (top-left dark, bottom-right light)
                r->DrawRectangle(Vector2(trackStartX, trackY),
                    Vector2(trackUsableW, trackH), fc(MyColor(6, 7, 14, 255)), true);
                r->DrawRectangle(Vector2(trackStartX, trackY),
                    Vector2(trackUsableW, 1.0f), fc(MyColor(4, 5, 10, 255)), true);
                r->DrawRectangle(Vector2(trackStartX, trackY),
                    Vector2(1.0f, trackH), fc(MyColor(4, 5, 10, 255)), true);
                r->DrawRectangle(Vector2(trackStartX, trackY + trackH - 1.0f),
                    Vector2(trackUsableW, 1.0f), fc(MyColor(62, 70, 105, 200)), true);
                r->DrawRectangle(Vector2(trackStartX + trackUsableW - 1.0f, trackY),
                    Vector2(1.0f, trackH), fc(MyColor(62, 70, 105, 200)), true);
                // Inner trough (recessed channel)
                r->DrawRectangle(Vector2(trackStartX + 1.0f, trackY + 1.0f),
                    Vector2(trackUsableW - 2.0f, trackH - 2.0f),
                    fc(MyColor(12, 15, 28, 255)), true);

                // Progress fill — two-tone gradient (top lighter, bottom darker)
                float t = (control.sliderMax > control.sliderMin)
                    ? std::clamp((control.sliderValue - control.sliderMin)
                                 / (control.sliderMax - control.sliderMin), 0.0f, 1.0f)
                    : 0.0f;
                float knobCentreX = trackStartX + t * trackUsableW;
                float fillW = knobCentreX - trackStartX - 1.0f;
                if (fillW > 1.5f) {
                    float fillH2 = std::floor((trackH - 2.0f) * 0.45f);
                    r->DrawRectangle(Vector2(trackStartX + 1.0f, trackY + 1.0f),
                        Vector2(fillW, fillH2), fc(MyColor(82, 158, 255, 220)), true);
                    r->DrawRectangle(Vector2(trackStartX + 1.0f, trackY + 1.0f + fillH2),
                        Vector2(fillW, trackH - 2.0f - fillH2),
                        fc(MyColor(42, 108, 218, 220)), true);
                }

                // Knob colour — gold when active (dragging), blue-hue otherwise
                bool flashOn = (GetTickCount64() / 500) % 2 == 0;
                MyColor knobTop, knobBot;
                if (control.isActive) {
                    knobTop = flashOn ? MyColor(255, 218, 82, 255) : MyColor(195, 162, 38, 255);
                    knobBot = flashOn ? MyColor(195, 148, 18, 255) : MyColor(138, 108, 12, 255);
                } else if (control.isHovered) {
                    knobTop = MyColor(160, 182, 248, 255);
                    knobBot = MyColor(88, 110, 205, 255);
                } else {
                    knobTop = MyColor(108, 118, 182, 255);
                    knobBot = MyColor(58, 65, 128, 255);
                }

                float knobX = knobCentreX - knobW * 0.5f;
                float ky    = control.position.y;
                float kh    = control.size.y;

                // Knob drop shadow
                r->DrawRectangle(Vector2(knobX + 2.0f, ky + 2.0f), Vector2(knobW, kh),
                    fc(MyColor(0, 0, 0, 115)), true);

                // Knob body — gradient: bright top half, dark bottom half
                float kHalf = std::floor(kh * 0.5f);
                r->DrawRectangle(Vector2(knobX, ky), Vector2(knobW, kHalf), fc(knobTop), true);
                r->DrawRectangle(Vector2(knobX, ky + kHalf), Vector2(knobW, kh - kHalf), fc(knobBot), true);

                // Knob outer raised bevel (3-D lifted edges)
                r->DrawRectangle(Vector2(knobX, ky), Vector2(knobW, 1.0f),
                    fc(MyColor(255, 255, 255, 92)), true);
                r->DrawRectangle(Vector2(knobX, ky), Vector2(1.0f, kh),
                    fc(MyColor(255, 255, 255, 82)), true);
                r->DrawRectangle(Vector2(knobX, ky + kh - 1.0f), Vector2(knobW, 1.0f),
                    fc(MyColor(0, 0, 0, 145)), true);
                r->DrawRectangle(Vector2(knobX + knobW - 1.0f, ky), Vector2(1.0f, kh),
                    fc(MyColor(0, 0, 0, 145)), true);

                // Inner bevel (second depth level)
                r->DrawRectangle(Vector2(knobX + 1.0f, ky + 1.0f), Vector2(knobW - 2.0f, 1.0f),
                    fc(MyColor(255, 255, 255, 50)), true);
                r->DrawRectangle(Vector2(knobX + 1.0f, ky + kh - 2.0f), Vector2(knobW - 2.0f, 1.0f),
                    fc(MyColor(0, 0, 0, 82)), true);

                // Centre grip lines (3 paired vertical notches — tactile indicator)
                {
                    float gx = knobCentreX - 5.0f;   // shifted 2px left per spec
                    float gy = ky + 3.0f;
                    float gh = kh - 6.0f;
                    for (int gi = 0; gi < 3; ++gi) {
                        r->DrawRectangle(Vector2(gx + gi * 3.0f, gy),
                            Vector2(1.0f, gh), fc(MyColor(0, 0, 0, 88)), true);
                        r->DrawRectangle(Vector2(gx + gi * 3.0f + 1.0f, gy),
                            Vector2(1.0f, gh), fc(MyColor(255, 255, 255, 38)), true);
                    }
                }
                break;
            }

            // -----------------------------------------------------------------
            // Panel — 3D raised or sunken decorative panel
            // sliderValue >= 0.5 = raised;  < 0.5 = sunken
            // -----------------------------------------------------------------
            case GUIControlType::Panel: {
                bool raised = (control.sliderValue >= 0.5f);

                // Drop shadow (raised panels cast a shadow; sunken panels are inset)
                if (raised) {
                    r->DrawRectangle(
                        Vector2(control.position.x + 4.0f, control.position.y + 4.0f),
                        control.size,
                        fc(MyColor(0, 0, 0, 90)), true);
                }

                // Main panel background
                r->DrawRectangle(control.position, control.size, fc(control.bgColor), true);

                // Texture drawn on top of flat background (e.g. portrait images)
                if (control.bgTextureId != -1) {
                    int texId = (control.isHovered && control.bgTextureHoverId != -1)
                                ? control.bgTextureHoverId : control.bgTextureId;
                    r->DrawTexture(texId, control.position, control.size, fc(MyColor(255, 255, 255, 255)), true);
                }

                // Outer bevel edges
                // Raised: top/left bright, bottom/right dark — "sticking out of the surface"
                // Sunken: top/left dark, bottom/right bright — "pushed into the surface"
                MyColor outerHigh = raised ? MyColor(88, 94, 118, 220) : MyColor(8, 10, 18, 230);
                MyColor outerShad = raised ? MyColor(8, 10, 18, 220)   : MyColor(88, 94, 118, 220);
                r->DrawRectangle(Vector2(control.position.x, control.position.y),
                    Vector2(control.size.x, 1.0f), fc(outerHigh), true); // top
                r->DrawRectangle(Vector2(control.position.x, control.position.y),
                    Vector2(1.0f, control.size.y), fc(outerHigh), true); // left
                r->DrawRectangle(Vector2(control.position.x, control.position.y + control.size.y - 1.0f),
                    Vector2(control.size.x, 1.0f), fc(outerShad), true); // bottom
                r->DrawRectangle(Vector2(control.position.x + control.size.x - 1.0f, control.position.y),
                    Vector2(1.0f, control.size.y), fc(outerShad), true); // right

                // Inner bevel (second level of depth)
                MyColor innerHigh = raised ? MyColor(60, 65, 84, 160) : MyColor(14, 16, 26, 180);
                MyColor innerShad = raised ? MyColor(14, 16, 26, 160) : MyColor(60, 65, 84, 180);
                r->DrawRectangle(Vector2(control.position.x + 1.0f, control.position.y + 1.0f),
                    Vector2(control.size.x - 2.0f, 1.0f), fc(innerHigh), true);
                r->DrawRectangle(Vector2(control.position.x + 1.0f, control.position.y + 1.0f),
                    Vector2(1.0f, control.size.y - 2.0f), fc(innerHigh), true);
                r->DrawRectangle(Vector2(control.position.x + 1.0f, control.position.y + control.size.y - 2.0f),
                    Vector2(control.size.x - 2.0f, 1.0f), fc(innerShad), true);
                r->DrawRectangle(Vector2(control.position.x + control.size.x - 2.0f, control.position.y + 1.0f),
                    Vector2(1.0f, control.size.y - 2.0f), fc(innerShad), true);

                // Render label text if present (left-aligned, vertically centred)
                if (!control.label.empty()) {
                    const float approxH = control.lblFontSize * 1.25f;
                    const float centredY = control.position.y + (control.size.y - approxH) * 0.5f;
                    r->DrawMyText(control.label,
                        Vector2(control.position.x + 6.0f, centredY),
                        fc(control.txtColor), control.lblFontSize);
                }
                break;
            }

            // -----------------------------------------------------------------
            // TextInput — editable single-line text field
            // -----------------------------------------------------------------
            case GUIControlType::TextInput: {
                const float px = control.position.x;
                const float py = control.position.y;
                const float pw = control.size.x;
                const float ph = control.size.y;

                // Soft drop shadow behind the field
                r->DrawRectangle(Vector2(px + 2.0f, py + 2.0f), control.size,
                    fc(MyColor(0, 0, 0, 70)), true);

                // Outer sunken frame (dark perimeter — top/left darker = sunken look)
                r->DrawRectangle(control.position, control.size,
                    fc(MyColor(8, 10, 18, 255)), true);
                // Bottom/right counter-highlights (lighter = make it look like it goes in)
                r->DrawRectangle(Vector2(px, py + ph - 1.0f), Vector2(pw, 1.0f),
                    fc(MyColor(65, 70, 92, 220)), true);
                r->DrawRectangle(Vector2(px + pw - 1.0f, py), Vector2(1.0f, ph),
                    fc(MyColor(65, 70, 92, 220)), true);

                // Inner background
                MyColor innerBg = control.isFocused
                    ? MyColor(18, 22, 38, 255) : MyColor(12, 15, 24, 255);
                r->DrawRectangle(Vector2(px + 1.0f, py + 1.0f),
                    Vector2(pw - 2.0f, ph - 2.0f), fc(innerBg), true);

                // Focus ring — blue-glow outline when the field has keyboard focus
                if (control.isFocused) {
                    r->DrawRectangle(Vector2(px, py), Vector2(pw, 1.0f),
                        fc(MyColor(45, 105, 210, 200)), true);
                    r->DrawRectangle(Vector2(px, py), Vector2(1.0f, ph),
                        fc(MyColor(45, 105, 210, 200)), true);
                    r->DrawRectangle(Vector2(px, py + ph - 1.0f), Vector2(pw, 1.0f),
                        fc(MyColor(45, 105, 210, 200)), true);
                    r->DrawRectangle(Vector2(px + pw - 1.0f, py), Vector2(1.0f, ph),
                        fc(MyColor(45, 105, 210, 200)), true);
                }

                // Shine strip along the top inner edge (depth illusion)
                r->DrawRectangle(Vector2(px + 2.0f, py + 1.0f), Vector2(pw - 4.0f, 1.0f),
                    fc(MyColor(255, 255, 255, 10)), true);

                // --- Text + cursor ---
                const float fs    = control.lblFontSize > 0.0f ? control.lblFontSize : 12.0f;
                const float textX = px + 6.0f;
                // Vertically centre text within the field
                const float approxH = fs * 1.25f;
                const float textY   = py + (ph - approxH) * 0.5f;

                if (control.inputText.empty() && !control.placeholder.empty()) {
                    // Placeholder hint in muted grey (never bold)
                    r->DrawMyText(control.placeholder, Vector2(textX, textY),
                        fc(MyColor(80, 85, 108, 200)), fs);
                }
                else if (control.bold) {
                    TextRenderStyle style;
                    style.fontSize = fs;
                    style.bold     = true;
                    r->DrawMyTextStyled(control.inputText, Vector2(textX, textY),
                        fc(control.txtColor), style);
                }
                else {
                    r->DrawMyText(control.inputText, Vector2(textX, textY),
                        fc(control.txtColor), fs);
                }

                // Blinking cursor bar (fires at ~1 Hz)
                if (control.isFocused) {
                    bool showCursor = (GetTickCount64() / 530) % 2 == 0;
                    if (showCursor) {
                        // Sum character widths up to cursorPos for precise placement
                        float cursorOffX = 0.0f;
                        int limit = std::min(control.cursorPos, (int)control.inputText.size());
                        for (int ci = 0; ci < limit; ++ci)
                            cursorOffX += r->GetCharacterWidth(control.inputText[ci], fs, control.bold);
                        r->DrawRectangle(
                            Vector2(textX + cursorOffX, py + 3.0f),
                            Vector2(2.0f, ph - 6.0f),
                            fc(MyColor(190, 205, 235, 255)), true);
                    }
                }
                break;
            }

            // -----------------------------------------------------------------
            // ListBox — scrollable selectable list with embedded scrollbar
            // Scrollbar occupies the rightmost 12 px of the control.
            // isActive = true while the scrollbar thumb is being dragged.
            // -----------------------------------------------------------------
            case GUIControlType::ListBox: {
                const float lx  = control.position.x;
                const float ly  = control.position.y;
                const float lw  = control.size.x;
                const float lh  = control.size.y;
                const float sbW = 12.0f;                    // scrollbar track width
                const float iH  = static_cast<float>(control.listItemHeight > 0 ? control.listItemHeight : 22);
                const int   visCount = static_cast<int>(lh / iH);
                const int   total    = static_cast<int>(control.items.size());

                // Drop shadow
                r->DrawRectangle(Vector2(lx + 3.0f, ly + 3.0f), control.size,
                    fc(MyColor(0, 0, 0, 85)), true);

                // Outer sunken frame
                r->DrawRectangle(control.position, control.size,
                    fc(MyColor(8, 10, 18, 255)), true);
                r->DrawRectangle(Vector2(lx, ly + lh - 1.0f), Vector2(lw, 1.0f),
                    fc(MyColor(70, 75, 96, 200)), true); // bottom highlight
                r->DrawRectangle(Vector2(lx + lw - 1.0f, ly), Vector2(1.0f, lh),
                    fc(MyColor(70, 75, 96, 200)), true); // right highlight

                // Item area background
                float itemAreaW = lw - sbW - 2.0f;
                r->DrawRectangle(Vector2(lx + 1.0f, ly + 1.0f),
                    Vector2(itemAreaW, lh - 2.0f), fc(MyColor(14, 17, 27, 255)), true);

                // Inner top shine (depth illusion)
                r->DrawRectangle(Vector2(lx + 2.0f, ly + 1.0f), Vector2(itemAreaW - 2.0f, 1.0f),
                    fc(MyColor(255, 255, 255, 8)), true);

                // --- Visible rows ---
                int start = std::max(0, control.listScrollOffset);
                for (int i = start; i < total && (i - start) < visCount; ++i) {
                    float rowY = ly + 1.0f + static_cast<float>(i - start) * iH;

                    // Row background — alternating shades + selected highlight
                    MyColor rowBg;
                    if (i == control.selectedIndex) {
                        // Selected: blue-tinted raised row
                        rowBg = control.isHovered ? MyColor(50, 98, 185, 220) : MyColor(38, 80, 160, 210);
                    } else {
                        rowBg = ((i % 2) == 0)
                            ? MyColor(14, 17, 27, 255)
                            : MyColor(18, 22, 34, 255);
                    }
                    r->DrawRectangle(Vector2(lx + 1.0f, rowY),
                        Vector2(itemAreaW, iH), fc(rowBg), true);

                    // Selected-row inner glow (top thin line)
                    if (i == control.selectedIndex) {
                        r->DrawRectangle(Vector2(lx + 1.0f, rowY),
                            Vector2(itemAreaW, 1.0f), fc(MyColor(80, 130, 220, 120)), true);
                    }

                    // Row text (4px left padding)
                    MyColor txtCol = (i == control.selectedIndex)
                        ? MyColor(240, 245, 255, 255) : MyColor(195, 200, 218, 255);
                    // Directories are tinted slightly gold
                    if (!control.items[i].empty() && control.items[i][0] == L'\u25BA')
                        txtCol = MyColor(235, 205, 120, 255);

                    const float fs = control.lblFontSize > 0.0f ? control.lblFontSize : 12.0f;
                    r->DrawMyText(control.items[i], Vector2(lx + 5.0f, rowY + 3.0f),
                        fc(txtCol), fs);
                }

                // --- Scrollbar track ---
                float sbX = lx + lw - sbW - 1.0f;
                r->DrawRectangle(Vector2(sbX, ly + 1.0f), Vector2(sbW, lh - 2.0f),
                    fc(MyColor(16, 19, 30, 255)), true);
                r->DrawRectangle(Vector2(sbX, ly + 1.0f), Vector2(1.0f, lh - 2.0f),
                    fc(MyColor(6, 8, 14, 200)), true); // left shadow of track

                // Scrollbar thumb
                if (total > visCount) {
                    float ratio    = static_cast<float>(visCount) / static_cast<float>(total);
                    float thumbH   = std::max(20.0f, (lh - 2.0f) * ratio);
                    float maxScr   = static_cast<float>(total - visCount);
                    float scrollT  = (maxScr > 0.0f) ? static_cast<float>(control.listScrollOffset) / maxScr : 0.0f;
                    float thumbY   = ly + 1.0f + scrollT * ((lh - 2.0f) - thumbH);

                    // Thumb shadow
                    r->DrawRectangle(Vector2(sbX + 1.0f, thumbY + 1.0f),
                        Vector2(sbW - 1.0f, thumbH), fc(MyColor(0, 0, 0, 80)), true);
                    // Thumb body
                    MyColor thumbCol = control.isActive
                        ? MyColor(80, 92, 130, 255) : MyColor(55, 62, 88, 255);
                    r->DrawRectangle(Vector2(sbX, thumbY),
                        Vector2(sbW, thumbH), fc(thumbCol), true);
                    // Thumb top highlight (3D raised)
                    r->DrawRectangle(Vector2(sbX, thumbY),
                        Vector2(sbW, 1.0f), fc(MyColor(95, 108, 150, 200)), true);
                    r->DrawRectangle(Vector2(sbX, thumbY),
                        Vector2(1.0f, thumbH), fc(MyColor(95, 108, 150, 180)), true);
                    // Thumb bottom shadow
                    r->DrawRectangle(Vector2(sbX, thumbY + thumbH - 1.0f),
                        Vector2(sbW, 1.0f), fc(MyColor(12, 14, 22, 200)), true);
                    // Grip lines (centre horizontal notches)
                    float midY = thumbY + thumbH * 0.5f;
                    for (int gi = -1; gi <= 1; ++gi) {
                        r->DrawRectangle(Vector2(sbX + 2.0f, midY + gi * 4.0f),
                            Vector2(sbW - 4.0f, 1.0f), fc(MyColor(30, 34, 50, 180)), true);
                        r->DrawRectangle(Vector2(sbX + 2.0f, midY + gi * 4.0f - 1.0f),
                            Vector2(sbW - 4.0f, 1.0f), fc(MyColor(90, 100, 140, 100)), true);
                    }
                }
                break;
            }

            // -----------------------------------------------------------------
            // ComboBox — closed: button + selected text; open: dropdown list
            // The dropdown panel is rendered in a second pass (after the main
            // loop) so it appears on top of all other controls.
            // -----------------------------------------------------------------
            case GUIControlType::ComboBox: {
                const float cx  = control.position.x;
                const float cy  = control.position.y;
                const float cw  = control.size.x;
                const float ch  = control.size.y;
                const float btnW = 22.0f;                // ▼ button width

                // Drop shadow
                r->DrawRectangle(Vector2(cx + 2.0f, cy + 2.0f), control.size,
                    fc(MyColor(0, 0, 0, 80)), true);

                // Outer raised frame
                r->DrawRectangle(control.position, control.size,
                    fc(MyColor(48, 52, 70, 255)), true);
                // Top/left highlight (raised edge)
                r->DrawRectangle(Vector2(cx, cy), Vector2(cw, 1.0f),
                    fc(MyColor(82, 88, 112, 220)), true);
                r->DrawRectangle(Vector2(cx, cy), Vector2(1.0f, ch),
                    fc(MyColor(82, 88, 112, 220)), true);
                // Bottom/right shadow (raised edge)
                r->DrawRectangle(Vector2(cx, cy + ch - 1.0f), Vector2(cw, 1.0f),
                    fc(MyColor(10, 12, 20, 220)), true);
                r->DrawRectangle(Vector2(cx + cw - 1.0f, cy), Vector2(1.0f, ch),
                    fc(MyColor(10, 12, 20, 220)), true);

                // Main inner background (text area, excluding ▼ button)
                float textAreaW = cw - btnW - 2.0f;
                r->DrawRectangle(Vector2(cx + 1.0f, cy + 1.0f),
                    Vector2(textAreaW, ch - 2.0f), fc(MyColor(14, 17, 27, 255)), true);

                // ▼ button face (raised pill on the right)
                bool btnHov = control.isHovered;
                MyColor btnFace = btnHov
                    ? MyColor(70, 80, 118, 255) : MyColor(40, 46, 66, 255);
                float btnX = cx + cw - btnW - 1.0f;
                r->DrawRectangle(Vector2(btnX, cy + 1.0f),
                    Vector2(btnW, ch - 2.0f), fc(btnFace), true);
                // Button top highlight
                r->DrawRectangle(Vector2(btnX, cy + 1.0f), Vector2(btnW, 1.0f),
                    fc(MyColor(90, 100, 140, 180)), true);
                r->DrawRectangle(Vector2(btnX, cy + 1.0f), Vector2(1.0f, ch - 2.0f),
                    fc(MyColor(85, 95, 132, 160)), true);
                // Button bottom shadow
                r->DrawRectangle(Vector2(btnX, cy + ch - 2.0f), Vector2(btnW, 1.0f),
                    fc(MyColor(8, 10, 18, 180)), true);
                // Divider line between text area and button
                r->DrawRectangle(Vector2(btnX - 1.0f, cy + 2.0f), Vector2(1.0f, ch - 4.0f),
                    fc(MyColor(28, 32, 48, 255)), true);

                // ▼ arrow centred in button
                r->DrawMyTextCentered(L"▼",
                    Vector2(btnX, cy), fc(MyColor(195, 205, 228, 225)),
                    9.0f, btnW, ch, false);

                // Selected item text
                if (control.selectedIndex >= 0 &&
                    control.selectedIndex < (int)control.items.size()) {
                    const float fs = control.lblFontSize > 0.0f ? control.lblFontSize : 12.0f;
                    const float approxH = fs * 1.25f;
                    const float textY   = cy + (ch - approxH) * 0.5f;
                    r->DrawMyText(control.items[control.selectedIndex],
                        Vector2(cx + 6.0f, textY), fc(control.txtColor), fs);
                }

                // NOTE: The open dropdown is rendered in a SECOND PASS after
                // this main loop so it appears on top of every other control.
                break;
            }
        }
    }

    // --- Second pass: open ComboBox dropdowns (must paint over all other controls) ---
    for (auto& control : controls) {
        if (bWindowDestroy) break;
        if (control.type != GUIControlType::ComboBox) continue;
        if (!control.isVisible || !control.isDropdownOpen) continue;

        const float cx   = control.position.x;
        const float cy   = control.position.y;
        const float cw   = control.size.x;
        const float ch   = control.size.y;
        const int   total    = static_cast<int>(control.items.size());
        const int   maxRows  = std::min(control.dropdownMaxRows, total);
        const float iH       = static_cast<float>(control.listItemHeight > 0 ? control.listItemHeight : 22);
        const float dropH    = static_cast<float>(maxRows) * iH + 4.0f;

        // Position dropdown: below the control normally;
        // flip above if not enough room (simplistic — assumes 600px screen min)
        float dropY = cy + ch;

        // Dropdown shadow
        r->DrawRectangle(Vector2(cx + 4.0f, dropY + 4.0f), Vector2(cw, dropH),
            fc(MyColor(0, 0, 0, 110)), true);

        // Dropdown outer panel (raised)
        r->DrawRectangle(Vector2(cx, dropY), Vector2(cw, dropH),
            fc(MyColor(32, 36, 52, 255)), true);
        // Outer bevel edges
        r->DrawRectangle(Vector2(cx, dropY), Vector2(cw, 1.0f),
            fc(MyColor(80, 88, 115, 220)), true);
        r->DrawRectangle(Vector2(cx, dropY), Vector2(1.0f, dropH),
            fc(MyColor(80, 88, 115, 220)), true);
        r->DrawRectangle(Vector2(cx, dropY + dropH - 1.0f), Vector2(cw, 1.0f),
            fc(MyColor(8, 10, 18, 220)), true);
        r->DrawRectangle(Vector2(cx + cw - 1.0f, dropY), Vector2(1.0f, dropH),
            fc(MyColor(8, 10, 18, 220)), true);
        // Inner bevel
        r->DrawRectangle(Vector2(cx + 1.0f, dropY + 1.0f), Vector2(cw - 2.0f, 1.0f),
            fc(MyColor(55, 62, 85, 160)), true);

        // Render visible items
        for (int i = 0; i < maxRows; ++i) {
            float rowY = dropY + 2.0f + static_cast<float>(i) * iH;
            MyColor rowBg;
            if (i == control.selectedIndex) {
                rowBg = MyColor(42, 84, 168, 210);
            } else if (control.isHovered && i == control.selectedIndex) {
                rowBg = MyColor(50, 95, 180, 220);
            } else {
                rowBg = ((i % 2) == 0)
                    ? MyColor(32, 36, 52, 255)
                    : MyColor(26, 30, 44, 255);
            }
            r->DrawRectangle(Vector2(cx + 1.0f, rowY), Vector2(cw - 2.0f, iH), fc(rowBg), true);

            // Selected row glow line
            if (i == control.selectedIndex) {
                r->DrawRectangle(Vector2(cx + 1.0f, rowY), Vector2(cw - 2.0f, 1.0f),
                    fc(MyColor(70, 120, 215, 130)), true);
            }

            MyColor txtCol = (i == control.selectedIndex)
                ? MyColor(240, 245, 255, 255) : MyColor(190, 198, 218, 255);
            const float fs = control.lblFontSize > 0.0f ? control.lblFontSize : 12.0f;
            r->DrawMyText(control.items[i], Vector2(cx + 8.0f, rowY + 3.0f), fc(txtCol), fs);
        }
    }

    // Ensure clip rect is popped before custom rendering (e.g. config scrollbar)
    // so that onCustomRender is never scissored.
    if (clipActive) {
        r->PopClipRect();
        clipActive = false;
    }

    // Custom rendering extension — drawn on top of all controls; used by windows
    // (e.g. ConsoleWindow) that require content beyond the generic GUIControl set.
    if (onCustomRender) {
        try { onCustomRender(r); }
        catch (...) {}
    }
}