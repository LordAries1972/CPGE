#include "DX11Renderer.h"
#include "DX_FXManager.h"
#include "RendererMacros.h"
#include "ThreadManager.h"
#include "SoundManager.h"
#include "GUIManager.h"
#include "WinSystem.h"
#include "Debug.h"

extern Vector2 myMouseCoords;
extern SoundManager soundManager;
extern WindowMetrics winMetrics;
extern FXManager fxManager;
extern ThreadManager threadManager;

void GUIManager::CreateAlertWindow(const std::wstring& message) {
    const std::string WINDOW_NAME = "AlertWindow";

    // Create the Alert Window using existing CreateMyWindow function
    CreateMyWindow(
        WINDOW_NAME,                                        // Window name
        GUIWindowType::Alert,                               // Window type (Alert)
        Vector2(200, 150),                                  // Position (x, y)
        Vector2(400, 300),                                  // Size (width, height)
        MyColor(120, 0, 0, 0),                              // Background color (very dark red)
        int(BlitObj2DIndexType::IMG_WINFRAME1)              // Background texture ID
    );

    // Get the created window with proper error checking
    std::shared_ptr<GUIWindow> alertWindow = GetWindow(WINDOW_NAME);
    if (!alertWindow) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateAlertWindow - Failed to create alert window");
        return;
    }

    // Add Title Bar control with corrected lambda handlers
    GUIControl titleBar; // Use stack-allocated control instead of shared_ptr to avoid circular references
    titleBar.type = GUIControlType::TitleBar;
    titleBar.position = Vector2(alertWindow->position.x, alertWindow->position.y);
    titleBar.size = Vector2(alertWindow->size.x - (CLOSEWINBUTTON_SIZE + 6), TITLEBAR_HEIGHT);
    titleBar.bgColor = MyColor(0, 0, 0, 255);
    titleBar.txtColor = MyColor(255, 255, 0, 255);
    titleBar.bgTextureId = int(BlitObj2DIndexType::IMG_TITLEBAR1);
    titleBar.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1HL);
    titleBar.label = L"   Alert Status!";
    titleBar.lblFontSize = 18.0f;
    titleBar.isVisible = true;

    // Fixed lambda handlers using weak_ptr to prevent circular references
    std::weak_ptr<GUIWindow> weakAlertWindow = alertWindow;

    titleBar.onMouseBtnDown = [weakAlertWindow]() {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakAlertWindow.lock()) {
            if (!window->bWindowDestroy) {
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CreateAlertWindow - TitleBar mouse down detected");
                // Set dragging state safely
                window->isDragging = true;
            }
        }
        };

    titleBar.onMouseBtnUp = [weakAlertWindow]() {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakAlertWindow.lock()) {
            if (!window->bWindowDestroy) {
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CreateAlertWindow - TitleBar mouse up detected");
                // Clear dragging state safely
                window->isDragging = false;
            }
        }
        };

    titleBar.onMouseMove = [weakAlertWindow]() {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakAlertWindow.lock()) {
            if (!window->bWindowDestroy) {
                // Handle dragging logic here if needed
            }
        }
        };

    // Add the control to the window
    alertWindow->AddControl(titleBar);

    // Set the content text for the window
    alertWindow->contentText = message;

    // Add Text Area control for displaying message content
    GUIControl textArea;
    textArea.type = GUIControlType::TextArea;
    textArea.position = Vector2(alertWindow->position.x + 6, alertWindow->position.y + (titleBar.size.y + 6));
    textArea.size = Vector2(alertWindow->size.x - 6 - (SCROLLBAR_WIDTH - 2), alertWindow->size.y - 74);
    textArea.lblFontSize = 14.0f;
    textArea.bgColor = MyColor(60, 0, 0, 255);
    textArea.txtColor = MyColor(0, 175, 255, 255);
    textArea.bgTextureId = int(BlitObj2DIndexType::IMG_BEVEL1);
    textArea.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BEVEL1);
    textArea.isVisible = true;
    alertWindow->AddControl(textArea);

    // Add Okay Button control with fixed lambda handler
    GUIControl okayButton;
    okayButton.type = GUIControlType::Button;
    okayButton.position = Vector2(alertWindow->position.x + (140 - winMetrics.borderWidth),
        (alertWindow->position.y + alertWindow->size.y) - 35);
    okayButton.size = Vector2(BUTTON_WIDTH, 30);
    okayButton.bgColor = MyColor(0, 0, 0, 255);
    okayButton.txtColor = MyColor(0, 80, 255, 255);
    okayButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    okayButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    okayButton.label = L"Ok";
    okayButton.lblFontSize = 16.0f;
    okayButton.isVisible = true;

    // Fixed lambda handler using weak reference to GUIManager
    okayButton.onMouseBtnDown = [this, windowName = std::string(WINDOW_NAME)]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateAlertWindow - Okay button clicked");

            // Play sound effect safely
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // Remove window safely
            RemoveWindow(windowName);
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateAlertWindow - Exception in okay button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
        };
    alertWindow->AddControl(okayButton);

    // Add Close Button control with fixed lambda handler
    GUIControl btnClose;
    btnClose.type = GUIControlType::Button;
    btnClose.position = Vector2((alertWindow->position.x + alertWindow->size.x) - (CLOSEWINBUTTON_SIZE + 4),
        alertWindow->position.y + 4);
    btnClose.size = Vector2(CLOSEWINBUTTON_SIZE, CLOSEWINBUTTON_SIZE);
    btnClose.bgColor = MyColor(120, 0, 0, 255);
    btnClose.txtColor = MyColor(80, 0, 0, 255);
    btnClose.bgTextureId = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
    btnClose.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
    btnClose.label = L"";
    btnClose.lblFontSize = 8.0f;
    btnClose.isVisible = true;

    // Fixed lambda handler for close button
    btnClose.onMouseBtnDown = [this, windowName = std::string(WINDOW_NAME)]()
    {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateAlertWindow - Close button clicked");

            // Play sound effect safely
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // Remove window safely
            RemoveWindow(windowName);
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateAlertWindow - Exception in close button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    };
    
    alertWindow->AddControl(btnClose);
}

void GUIManager::CreateGameMenuWindow(const std::wstring& message) {
    const std::string WINDOW_NAME = "GameMenuWindow";

    // Use debug output to log function entry
    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Creating game menu window with message: %s", message.c_str());

    // Get renderer dimensions safely using WithDX11Renderer pattern from existing code
    WithDX11Renderer([this, WINDOW_NAME](std::shared_ptr<DX11Renderer> dx11) {
        // Validate DX11 renderer before proceeding
        if (!dx11) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - DX11 renderer is null, cannot create window");
            return;
        }

        // Create the Game Menu Window with proper error checking
        CreateMyWindow(
            WINDOW_NAME,                                                    // Window name
            GUIWindowType::Dialog,                                          // Window type (Dialog)
            Vector2(dx11->iOrigWidth - 305, 0),                             // Position (x, y) - right side of screen
            Vector2(300, dx11->iOrigHeight),                                // Size (width, height) - full height
            MyColor(0, 0, 0, 0),                                            // Background color (transparent black)
            int(BlitObj2DIndexType::NONE)                                   // No background texture ID
        );

        // Log successful window creation with dimensions
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CreateGameMenuWindow - Window created at position (%d, %d) with size (%d, %d)",
            dx11->iOrigWidth - 305, 0, 300, dx11->iOrigHeight);
        });

    // Get the created window with proper error checking
    std::shared_ptr<GUIWindow> gameMenuWindow = GetWindow(WINDOW_NAME);
    if (!gameMenuWindow) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Failed to create game menu window");
        return;
    }

    // Create weak reference to prevent circular references in lambda handlers
    std::weak_ptr<GUIWindow> weakGameMenuWindow = gameMenuWindow;

    // Add Title Bar control with corrected implementation
    GUIControl titleBar; // Use stack-allocated control to avoid circular references
    titleBar.type = GUIControlType::TitleBar;
    titleBar.position = Vector2(gameMenuWindow->position.x, gameMenuWindow->position.y);    // Position at top of window
    titleBar.size = Vector2(gameMenuWindow->size.x, 40);                                    // Height of 40 pixels
    titleBar.bgColor = MyColor(0, 0, 0, 255);                                               // Black background
    titleBar.txtColor = MyColor(255, 255, 0, 255);                                          // Yellow text
    titleBar.bgTextureId = int(BlitObj2DIndexType::IMG_TITLEBAR2);                          // Background texture
    titleBar.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR2);                     // Hover texture (same as background)
    titleBar.label = L"";                                                                   // Empty title text
    titleBar.lblFontSize = 18.0f;                                                           // Font size for title
    titleBar.isVisible = true;                                                              // Make control visible

    // Note: Title bar for dialog windows typically doesn't need drag functionality
    // Add the title bar control to the window
    gameMenuWindow->AddControl(titleBar);

    // Add Configuration Button control with fixed lambda handlers
    GUIControl configButton;
    configButton.type = GUIControlType::Button;
    configButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 55);        // Position inside the window
    configButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Size of the button
    configButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    configButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    configButton.useShadowedText = true;                                                                       // Enable text shadowing
    configButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    configButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    configButton.label = L"      CONFIGURATION";                                                              // Button label text
    configButton.lblFontSize = 16.0f;                                                                         // Font size for button text
    configButton.isVisible = true;                                                                            // Make button visible

    // Fixed onMouseOver handler using weak reference
    configButton.onMouseOver = [weakGameMenuWindow]() {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakGameMenuWindow.lock()) {
            if (!window->bWindowDestroy) {

                // Create outline effect for button hover (commented out FX manager call as per original code)
                // Vector2 pos = configButton.position;
                // Vector2 size = configButton.size;
                // XMFLOAT4 yellow(1.0f, 1.0f, 0.0f, 0.7f);
                // fxManager.CreateOutlineFX(yellow, pos, size, 1, true, 7, 5);
            }
        }
        };

    // Fixed onMouseBtnDown handler using weak reference and proper error handling
    configButton.onMouseBtnDown = [this]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Configuration button clicked");

            // Play sound effect safely
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // TODO: Add configuration window creation or scene transition logic here
            // Note: Original code was commented out, keeping window open as intended

        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Exception in configuration button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
        };

    // Add the configuration button control to the window
    gameMenuWindow->AddControl(configButton);

    // Add Game Play Button control with fixed lambda handlers
    GUIControl gameplayButton;
    gameplayButton.type = GUIControlType::Button;
    gameplayButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 110);       // Position below configuration button
    gameplayButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Same size as other buttons
    gameplayButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    gameplayButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    gameplayButton.useShadowedText = true;                                                                       // Enable text shadowing
    gameplayButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    gameplayButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    gameplayButton.label = L"        GAME PLAY";                                                                // Button label text
    gameplayButton.lblFontSize = 16.0f;                                                                         // Font size for button text
    gameplayButton.isVisible = true;                                                                            // Make button visible

    // Fixed onMouseOver handler using weak reference
    gameplayButton.onMouseOver = [weakGameMenuWindow]() {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakGameMenuWindow.lock()) {
            if (!window->bWindowDestroy) {

                // Create outline effect for button hover (commented out FX manager call as per original code)
                // Vector2 pos = gameplayButton.position;
                // Vector2 size = gameplayButton.size;
                // XMFLOAT4 yellow(1.0f, 1.0f, 0.0f, 1.0f);
                // fxManager.CreateOutlineFX(yellow, pos, size, 1, true, 7, 5);
            }
        }
        };

    // Fixed onMouseBtnDown handler using proper error handling
    gameplayButton.onMouseBtnDown = [this]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Game Play button clicked");

            // Play sound effect safely
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // TODO: Add gameplay scene transition logic here
            // Note: Original code was commented out, keeping window open as intended

        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Exception in gameplay button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
        };

    // Add the gameplay button control to the window
    gameMenuWindow->AddControl(gameplayButton);

    // Add Hi-Scores Table Button control with fixed lambda handlers
    GUIControl hiscoresButton;
    hiscoresButton.type = GUIControlType::Button;
    hiscoresButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 165);       // Position below gameplay button
    hiscoresButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Same size as other buttons
    hiscoresButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    hiscoresButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    hiscoresButton.useShadowedText = true;                                                                       // Enable text shadowing
    hiscoresButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    hiscoresButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    hiscoresButton.label = L"       HIGH SCORES";                                                               // Button label text
    hiscoresButton.lblFontSize = 16.0f;                                                                         // Font size for button text
    hiscoresButton.isVisible = true;                                                                            // Make button visible

    // Fixed onMouseOver handler using weak reference
    hiscoresButton.onMouseOver = [weakGameMenuWindow]() {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakGameMenuWindow.lock()) {
            if (!window->bWindowDestroy) {

                // Create outline effect for button hover (commented out FX manager call as per original code)
                // Vector2 pos = hiscoresButton.position;
                // Vector2 size = hiscoresButton.size;
                // XMFLOAT4 yellow(1.0f, 1.0f, 0.0f, 1.0f);
                // fxManager.CreateOutlineFX(yellow, pos, size, 1, true, 7, 5);
            }
        }
        };

    // Fixed onMouseBtnDown handler using proper error handling
    hiscoresButton.onMouseBtnDown = [this]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - High Scores button clicked");

            // Play sound effect safely
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // TODO: Add high scores window creation or scene transition logic here
            // Note: Original code was commented out, keeping window open as intended

        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Exception in high scores button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
        };

    // Add the high scores button control to the window
    gameMenuWindow->AddControl(hiscoresButton);

    // Add Credits Button control with fixed lambda handlers
    GUIControl creditsButton;
    creditsButton.type = GUIControlType::Button;
    creditsButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 220);       // Position below high scores button
    creditsButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Same size as other buttons
    creditsButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    creditsButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    creditsButton.useShadowedText = true;                                                                      // Enable text shadowing
    creditsButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    creditsButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    creditsButton.label = L"    SHOW CREDITS";                                                                 // Button label text
    creditsButton.lblFontSize = 16.0f;                                                                         // Font size for button text
    creditsButton.isVisible = true;                                                                            // Make button visible

    // Fixed onMouseOver handler using weak reference
    creditsButton.onMouseOver = [weakGameMenuWindow]() {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakGameMenuWindow.lock()) {
            if (!window->bWindowDestroy) {

                // Create outline effect for button hover (commented out FX manager call as per original code)
                // Vector2 pos = creditsButton.position;
                // Vector2 size = creditsButton.size;
                // XMFLOAT4 yellow(1.0f, 1.0f, 0.0f, 1.0f);
                // fxManager.CreateOutlineFX(yellow, pos, size, 1, true, 7, 5);
            }
        }
        };

    // Fixed onMouseBtnDown handler using proper error handling
    creditsButton.onMouseBtnDown = [this]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Credits button clicked");

            // Play sound effect safely
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // TODO: Add credits window creation or scene transition logic here
            // Note: Original code was commented out, keeping window open as intended

        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Exception in credits button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
        };

    // Add the credits button control to the window
    gameMenuWindow->AddControl(creditsButton);

    // Add Quit Button control with fixed lambda handlers
    GUIControl quitButton;
    quitButton.type = GUIControlType::Button;
    quitButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 275);       // Position below credits button
    quitButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Same size as other buttons
    quitButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    quitButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    quitButton.useShadowedText = true;                                                                      // Enable text shadowing
    quitButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    quitButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    quitButton.label = L"    QUIT TO DESKTOP";                                                              // Button label text
    quitButton.lblFontSize = 16.0f;                                                                         // Font size for button text
    quitButton.isVisible = true;                                                                            // Make button visible

    // Fixed onMouseOver handler using weak reference
    quitButton.onMouseOver = [weakGameMenuWindow]() {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakGameMenuWindow.lock()) {
            if (!window->bWindowDestroy) {
                // Create outline effect for button hover (commented out FX manager call as per original code)
                // Vector2 pos = quitButton.position;
                // Vector2 size = quitButton.size;
                // XMFLOAT4 yellow(1.0f, 1.0f, 0.0f, 1.0f);
                // fxManager.CreateOutlineFX(yellow, pos, size, 1, true, 7, 5);
            }
        }
        };

    // Fixed onMouseBtnDown handler with proper shutdown sequence and error handling
    quitButton.onMouseBtnDown = [this, windowName = std::string(WINDOW_NAME)]()
    {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Quit button clicked, initiating shutdown sequence");

            // Play sound effect safely
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // Initiate fade to black effect with proper timing
            fxManager.FadeToBlack(1.0f, 0.06f);

            // Wait for fade effect to complete with proper timeout to prevent infinite loop
            int fadeTimeout = 0;
            const int MAX_FADE_TIMEOUT = 300; // 3 seconds maximum wait time (300 * 10ms)
            while (fxManager.IsFadeActive() && fadeTimeout < MAX_FADE_TIMEOUT) {
                Sleep(10); // Sleep for 10 milliseconds
                fadeTimeout++;
            }

            // Log fade completion status
            if (fadeTimeout >= MAX_FADE_TIMEOUT) {
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"CreateGameMenuWindow - Fade effect timeout reached, proceeding with shutdown");
            }
            else {
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CreateGameMenuWindow - Fade effect completed successfully");
            }

            // Remove the game menu window safely before application shutdown
            RemoveWindow(windowName);

            // Post quit message to initiate clean application shutdown
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Posting quit message for application shutdown");
            // state we are shutting down as this will stop the renderer.
            threadManager.threadVars.bIsShuttingDown.store(true);
            PostQuitMessage(0);

        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"CreateGameMenuWindow - Exception in quit button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());

            // Emergency shutdown if exception occurs
            debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"CreateGameMenuWindow - Emergency shutdown initiated due to exception");
            threadManager.threadVars.bIsShuttingDown.store(true);
            PostQuitMessage(0); // Exit with error code
        }
    };

    // Add the quit button control to the window
    gameMenuWindow->AddControl(quitButton);

    // Log successful completion of game menu window creation
    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Game menu window created successfully with %d controls",
        static_cast<int>(gameMenuWindow->controls.size()));
}
