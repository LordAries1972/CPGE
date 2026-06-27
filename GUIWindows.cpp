
#include "Includes.h"
#include "FXManager.h"

// Our required Classes to create the GUI windows
#include "ThreadManager.h"
#include "SoundManager.h"
#include "GUIManager.h"
#include "WinSystem.h"
#include "Debug.h"
#include "SceneManager.h"
#include "GamePlayer.h"
#include "Configuration.h"

extern Vector2 myMouseCoords;
extern SoundManager soundManager;
extern GamePlayer gamePlayer;
extern PlayerInfo playerInfo[MAX_PLAYERS]; // Player Info Array

extern void StopMusicPlayback();
extern WindowMetrics winMetrics;
extern FXManager fxManager;

// Known Window Names — extern so main.cpp and other TUs can share these via extern declarations.
extern const std::string DIFFICULTY_WINDOW_NAME    = "DifficultyWindow";
extern const std::string GameMenu_WindowName       = "GameMenuWindow";
extern const std::string GAMEPLAYTYPES_WINDOW_NAME = "GamePlayTypes";
extern const std::string QUIT_CONFIRM_WINDOW_NAME  = "QuitConfirmDialog";

#ifdef PROJECT_ONLY_CODE
    extern const std::string USERPROFILE_WINDOW_NAME   = "UserProfile";
    extern const std::string USERPROFILE_SHADOW_NAME   = "UserProfileShadow";
#endif

// Forward declaration of the renderer's scene switch function, 
// which is called by the experimental button in the game menu to 
// launch or demo demo scene when required.
extern ThreadManager threadManager;
extern SceneManager scene;
extern Debug debug;
extern Configuration config;
extern std::shared_ptr<Renderer> renderer;

#if defined(_WIN32) || defined(_WIN64)
    extern HWND hwnd;  // main window handle — PostMessage(hwnd, WM_CLOSE) reaches WndProc on the main thread
#endif

extern void SwitchToGameIntro();
extern void StartGame();

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

    // Hide during setup — same race guard as CreateConfigWindow.
    alertWindow->isVisible = false;

    // Add Title Bar control with corrected lambda handlers
    GUIControl titleBar; // Use stack-allocated control instead of shared_ptr to avoid circular references
    titleBar.type = GUIControlType::TitleBar;
    titleBar.position = Vector2(alertWindow->position.x, alertWindow->position.y);
    titleBar.size = Vector2(alertWindow->size.x - (CLOSEWINBUTTON_SIZE + 6), TITLEBAR_HEIGHT);
    titleBar.bgColor = MyColor(0, 0, 0, 255);
    titleBar.txtColor = MyColor(255, 255, 0, 255);
    titleBar.bgTextureId = int(BlitObj2DIndexType::IMG_TITLEBAR1);
    titleBar.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1HL);
    titleBar.label = L"Alert Status!";
    titleBar.lblFontSize = 18.0f;
    titleBar.isVisible = true;

    // Fixed lambda handlers using weak_ptr to prevent circular references
    std::weak_ptr<GUIWindow> weakAlertWindow = alertWindow;

    titleBar.onMouseBtnDown = [weakAlertWindow]() 
    {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakAlertWindow.lock()) 
        {
            if (!window->bWindowDestroy) {
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CreateAlertWindow - TitleBar mouse down detected");
                // Set dragging state safely
                window->isDragging = true;
            }
        }
    }; // End of mouse down handler

    titleBar.onMouseBtnUp = [weakAlertWindow]() 
    {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakAlertWindow.lock()) {
            if (!window->bWindowDestroy) {
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CreateAlertWindow - TitleBar mouse up detected");
                // Clear dragging state safely
                window->isDragging = false;
            }
        }
    }; // End of mouse up handler

    titleBar.onMouseMove = [weakAlertWindow]() 
    {
        // Use weak_ptr to avoid circular references and check validity
        if (auto window = weakAlertWindow.lock()) {
            if (!window->bWindowDestroy) {
                // Handle dragging logic here if needed
            }
        }
    }; // End of mouse move handler

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
    okayButton.onMouseBtnDown = [this, windowName = std::string(WINDOW_NAME)]() 
    {
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
    }; // End of mouse down handler
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
    }; // End of mouse down handler
    
    alertWindow->AddControl(btnClose);

    // All controls added — safe to make visible now.
    alertWindow->isVisible = true;
}

void GUIManager::CreateGameMenuWindow(const std::wstring& message) {
    // Use debug output to log function entry
    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Creating game menu window with message: %s", message.c_str());

    // Validate DX11 renderer before proceeding
    if (!renderer) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - DX11 renderer is null, cannot create window");
        return;
    }

    // Create the Game Menu Window with proper error checking
    CreateMyWindow(
        GameMenu_WindowName,                                                        // Window name
        GUIWindowType::Dialog,                                              // Window type (Dialog)
        Vector2(renderer->iOrigWidth - GAMEMENU_WINDOW_WIDTH, 0),           // Position (x, y) - right side of screen
        Vector2(GAMEMENU_WINDOW_WIDTH, renderer->iOrigHeight),              // Size (width, height) - full height
        MyColor(0, 0, 0, 0),                                                // Background color (transparent — controls provide all visual substance)
        int(BlitObj2DIndexType::NONE)                                       // No background texture ID
    );

    // Log successful window creation with dimensions
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"CreateGameMenuWindow - Window created at position (%d, %d) with size (%d, %d)",
        renderer->iOrigWidth - static_cast<int>(GAMEMENU_WINDOW_WIDTH), 0, static_cast<int>(GAMEMENU_WINDOW_WIDTH), renderer->iOrigHeight);

    // Get the created window with proper error checking
    std::shared_ptr<GUIWindow> gameMenuWindow = GetWindow(GameMenu_WindowName);
    if (!gameMenuWindow) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Failed to create game menu window");
        return;
    }

    // Hide during setup — same race guard as CreateConfigWindow.
    gameMenuWindow->isVisible = false;

    // Create weak reference to prevent circular references in lambda handlers
    std::weak_ptr<GUIWindow> weakGameMenuWindow = gameMenuWindow;

    // Add Title Bar control with corrected implementation
    GUIControl titleBar; // Use stack-allocated control to avoid circular references
    titleBar.type = GUIControlType::TitleBar;
    titleBar.position = Vector2(gameMenuWindow->position.x, gameMenuWindow->position.y);                    // Position at top of window
    titleBar.size = Vector2(gameMenuWindow->size.x, 40);                                                    // Height of 40 pixels
    titleBar.bgColor = MyColor(0, 0, 0, 255);                                                               // Black background
    titleBar.txtColor = MyColor(255, 255, 0, 255);                                                          // Yellow text
    titleBar.bgTextureId = int(BlitObj2DIndexType::IMG_TITLEBAR2);                                          // Background texture
    titleBar.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR2);                                     // Hover texture (same as background)
    titleBar.label = L"";                                                                                   // Empty title text
    titleBar.lblFontSize = 18.0f;                                                                           // Font size for title
    titleBar.isVisible = true;                                                                              // Make control visible

    // Note: Title bar for dialog windows typically doesn't need drag functionality
    // Add the title bar control to the window
    gameMenuWindow->AddControl(titleBar);

    // Add Configuration Button control with fixed lambda handlers
    GUIControl configButton;
    configButton.type = GUIControlType::Button;
    configButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 55);       // Position inside the window
    configButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                  // Size of the button
    configButton.bgColor = MyColor(0, 0, 0, 255);                                                            // Black background
    configButton.txtColor = MyColor(255, 255, 0, 255);                                                       // Yellow text color
    configButton.useShadowedText = true;                                                                     // Enable text shadowing
    configButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                       // Button up texture
    configButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                // Button hover texture
    configButton.label = L"CONFIGURATION";                                                             // Button label text

    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        configButton.lblFontSize = 13.0f;
        configButton.bold = true;
    #elif defined(__USE_DIRECTX_12__)
        configButton.lblFontSize = 16.0f;
        configButton.bold = true;
    #else
        configButton.lblFontSize = 16.0f;
    #endif

    configButton.isVisible = true;                                                                           // Make button visible

    // Fixed onMouseOver handler using weak reference
    configButton.onMouseOver = [weakGameMenuWindow]() 
    {
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
    }; // End of mouse over handler

    // Open the config window on mouse button release (standard click-complete convention)
    configButton.onMouseBtnUp = [this]() 
    {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Configuration button released, opening config window");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            CreateConfigWindow();
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Exception in configuration button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    }; // End of mouse button up handler
    
    // Add the configuration button control to the window
    gameMenuWindow->AddControl(configButton);

    // Add Game Play Button control with fixed lambda handlers
    GUIControl gameplayButton;
    gameplayButton.type = GUIControlType::Button;
    gameplayButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 100);       // Position below configuration button
    gameplayButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Same size as other buttons
    gameplayButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    gameplayButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    gameplayButton.useShadowedText = true;                                                                      // Enable text shadowing
    gameplayButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    gameplayButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    gameplayButton.label = L"GAME PLAY";
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        gameplayButton.lblFontSize = 13.0f;
        gameplayButton.bold = true;
    #elif defined(__USE_DIRECTX_12__)
        gameplayButton.lblFontSize = 16.0f;
        gameplayButton.bold = true;
    #else
        gameplayButton.lblFontSize = 16.0f;
    #endif
    gameplayButton.isVisible = true;                                                                            // Make button visible

    // Fixed onMouseOver handler using weak reference
    gameplayButton.onMouseOver = [weakGameMenuWindow]() 
    {
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
    }; // End of mouse over handler

    // Fixed onMouseBtnDown handler using proper error handling
    gameplayButton.onMouseBtnDown = [this, windowName = std::string(GameMenu_WindowName)]() 
    {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Game Play button clicked");

            // Play sound effect safely
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            // Fade out GameMenu, then create and fade in DifficultyWindow
            ApplyWindowFadeCallback(
                GUIWindowFadeType::FadeOut, 0.8f, windowName,
                [this, windowName]() {
                    RemoveWindow(windowName);
                    CreateDifficultyWindow();
                    ApplyWindowFade(GUIWindowFadeType::FadeIn, 1.0f, "DifficultyWindow");
                }
            );
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Exception in gameplay button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    }; // End of mouse down handler

    // Add the gameplay button control to the window
    gameMenuWindow->AddControl(gameplayButton);

#ifdef PROJECT_ONLY_CODE
    // Add Profile Button control — opens UserProfile window for commander selection.
    GUIControl profileButton;
    profileButton.type = GUIControlType::Button;
    profileButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 145);
    profileButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);
    profileButton.bgColor = MyColor(0, 0, 0, 255);
    profileButton.txtColor = MyColor(255, 255, 0, 255);
    profileButton.useShadowedText = true;
    profileButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    profileButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    profileButton.label = L"PROFILE";
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        profileButton.lblFontSize = 13.0f;
        profileButton.bold = true;
    #elif defined(__USE_DIRECTX_12__)
        profileButton.lblFontSize = 16.0f;
        profileButton.bold = true;
    #else
        profileButton.lblFontSize = 16.0f;
    #endif
    profileButton.isVisible = true;

    profileButton.onMouseOver = [weakGameMenuWindow]() {
        if (auto window = weakGameMenuWindow.lock()) {
            if (!window->bWindowDestroy) { }
        }
    };

    profileButton.onMouseBtnUp = [this]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Profile button released, opening UserProfile window");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            CreateUserProfileWindow();
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Exception in profile button handler: %hs", e.what());
        }
    };

    gameMenuWindow->AddControl(profileButton);
#endif // PROJECT_ONLY_CODE

    // Add Hi-Scores Table Button control with fixed lambda handlers
    GUIControl hiscoresButton;
    hiscoresButton.type = GUIControlType::Button;
    hiscoresButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 190);       // Position below profile button
    hiscoresButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Same size as other buttons
    hiscoresButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    hiscoresButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    hiscoresButton.useShadowedText = true;                                                                      // Enable text shadowing
    hiscoresButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    hiscoresButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    hiscoresButton.label = L"HIGH SCORES";                                                               // Button label text
    
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        hiscoresButton.lblFontSize = 13.0f;
        hiscoresButton.bold = true;
    #elif defined(__USE_DIRECTX_12__)
        hiscoresButton.lblFontSize = 16.0f;
        hiscoresButton.bold = true;
    #else
        hiscoresButton.lblFontSize = 16.0f;
    #endif
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
    creditsButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 235);       // Position below high scores button
    creditsButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Same size as other buttons
    creditsButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    creditsButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    creditsButton.useShadowedText = true;                                                                      // Enable text shadowing
    creditsButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    creditsButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    creditsButton.label = L"SHOW CREDITS";                                                                 // Button label text
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        creditsButton.lblFontSize = 13.0f;
        creditsButton.bold = true;
    #elif defined(__USE_DIRECTX_12__)
        creditsButton.lblFontSize = 16.0f;
        creditsButton.bold = true;
    #else
        creditsButton.lblFontSize = 16.0f;
    #endif
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
    quitButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 280);       // Position below credits button
    quitButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Same size as other buttons
    quitButton.bgColor = MyColor(0, 0, 0, 255);                                                             // Black background
    quitButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Yellow text color
    quitButton.useShadowedText = true;                                                                      // Enable text shadowing
    quitButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                        // Button up texture
    quitButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);                                 // Button hover texture
    quitButton.label = L"QUIT TO DESKTOP";                                                              // Button label text
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        quitButton.lblFontSize = 13.0f;
        quitButton.bold = true;
    #elif defined(__USE_DIRECTX_12__)
        quitButton.lblFontSize = 16.0f;
        quitButton.bold = true;
    #else
        quitButton.lblFontSize = 16.0f;
    #endif
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

    // Quit button: start a fade-to-black then shut down inside the callback.
    // The callback fires from fxManager.Render() (render thread) once progress>=1,
    // so we never block the render thread with Sleep — the fade is fully visible.
    quitButton.onMouseBtnDown = [this, windowName = std::string(GameMenu_WindowName)]()
    {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Quit button clicked, starting fade-out shutdown sequence");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            fxManager.FadeOutThenCallback(
                { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f, 0.06f,
                [windowName]() {
                    // Stop music immediately (safe to call from any thread).
                    StopMusicPlayback();
                    // Signal the render thread to exit its loop.
                    threadManager.threadVars.bIsShuttingDown.store(true);
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Fade complete, signalling main thread to close");
                    #if defined(_WIN32) || defined(_WIN64)
                        // This callback fires from inside the render thread while the
                        // current frame is still in progress.  Posting WM_CLOSE directly
                        // risks DestroyWindow racing the render thread's final Present.
                        // A short-lived helper thread waits ~100 ms (≥ 2 frames at 60fps)
                        // for the render thread to finish and exit, then posts WM_CLOSE
                        // so the main message loop can safely tear down the window.
                        std::thread([]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            PostMessage(hwnd, WM_CLOSE, 0, 0);
                        }).detach();
                    #else
                        PostQuitMessage(0);
                    #endif
                });
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"CreateGameMenuWindow - Exception in quit button handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
            threadManager.threadVars.bIsShuttingDown.store(true);
            #if defined(_WIN32) || defined(_WIN64)
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            #else
                PostQuitMessage(0);
            #endif
        }
    };

    // Add the quit button control to the window
    gameMenuWindow->AddControl(quitButton);

    // This is the Experimental button used only on debug builds
    // so that users can prepare and test certain effects or scenes that 
    // aren't ready for release or general use yet, without the need for 
    // a separate debug menu or command console.
    #if defined(_DEBUG)
        // Experimental button — DEBUG builds only; launches WarpDotTunnel demo
        GUIControl experimentalButton;
        experimentalButton.type = GUIControlType::Button;
        experimentalButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 325);
        experimentalButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);
        experimentalButton.bgColor = MyColor(0, 0, 0, 255);
        experimentalButton.txtColor = MyColor(255, 128, 0, 255);                                                       // Orange text — visually distinct from release buttons
        experimentalButton.useShadowedText = true;
        experimentalButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
        experimentalButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
        experimentalButton.label = L"** EXPERIMENTAL **";
        #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
            experimentalButton.lblFontSize = 13.0f;
            experimentalButton.bold = true;
        #elif defined(__USE_DIRECTX_12__)
            experimentalButton.lblFontSize = 16.0f;
            experimentalButton.bold = true;
        #else
            experimentalButton.lblFontSize = 16.0f;
        #endif

        experimentalButton.isVisible = true;

        experimentalButton.onMouseBtnDown = [this, windowName = std::string(GameMenu_WindowName)]() 
        {
            // The experimental button OnMouseBtnDown handler performs a quick fade to black, then hands 
            // off to the loader thread to launch the WarpDotTunnel scene.
            try {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Experimental button clicked, launching WarpDotTunnel");

                soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

                fxManager.FadeToBlack(2.0f, 0.06f);

                // This callback runs on the main thread (via HandleAllInput), so we CAN
                // sleep here. The separate render thread advances the fade independently.
                int fadeTimeout = 0;
                const int MAX_FADE_TIMEOUT = 300;
                while (fxManager.IsFadeActive() && fadeTimeout < MAX_FADE_TIMEOUT) {
                    Sleep(10);
                    fadeTimeout++;
                }

                // Screen is now black — stop the starfield immediately so it doesn't
                // flicker visible between the FadeOut completing and the loader thread
                // calling SaveAndSuspendFXForScene().
                fxManager.StopStarfield();

                // Close the game menu window to reveal the black screen behind it, and prevent it 
                // from visually interfering with the demo's own UI elements (like the movie playback progress bar).
                RemoveWindow(windowName);

                // Screen is black — hand off to the loader thread which owns all
                // scene initialisation (SaveAndSuspendFXForScene + Init3DWarpDOTTunnel
                // + FadeToImage are all called from SCENE_EXPERIMENT in IOLoaderThread.cpp).
                scene.SetGotoScene(SCENE_EXPERIMENT);
                scene.InitiateScene();
                scene.SetGotoScene(SCENE_NONE);
                // Start the loader thread's scene setup, which will eventually call SaveAndSuspendFXForScene()
                renderer->ResumeLoader();
            }
            catch (const std::exception& e) 
            {
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGameMenuWindow - Exception in experimental button handler: %s",
                    std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
            }
        };
        // Add button to window
        gameMenuWindow->AddControl(experimentalButton);
    #endif

    // All controls added — safe to make visible now.
    gameMenuWindow->isVisible = true;

    // Log successful completion of game menu window creation
    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGameMenuWindow - Game menu window created successfully with %d controls",
        static_cast<int>(gameMenuWindow->controls.size()));
}

//-------------------------------------------------------------------------------------------------
// CreateDifficultyWindow — full-height side panel matching the GameMenu style.
// Buttons are centred horizontally and vertically within the content area.
// Call: guiManager.CreateDifficultyWindow();
//-------------------------------------------------------------------------------------------------
void GUIManager::CreateDifficultyWindow() {
    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateDifficultyWindow - Creating difficulty selection window");

    if (!renderer) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateDifficultyWindow - renderer is null, cannot create window");
        return;
    }

    // Create window — same position and dimensions as GameMenuWindow
    CreateMyWindow(
        DIFFICULTY_WINDOW_NAME,                                                        // Window name
        GUIWindowType::Dialog,                                              // Dialog type (no close chrome)
        Vector2(renderer->iOrigWidth - GAMEMENU_WINDOW_WIDTH, 0),          // Right-side panel, full height
        Vector2(GAMEMENU_WINDOW_WIDTH, renderer->iOrigHeight),             // Full screen height
        MyColor(0, 0, 0, 0),                                               // Transparent background — textures supply visuals
        int(BlitObj2DIndexType::NONE)                                       // No background texture
    );

    std::shared_ptr<GUIWindow> diffWindow = GetWindow(DIFFICULTY_WINDOW_NAME);
    if (!diffWindow) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateDifficultyWindow - Failed to retrieve window after creation");
        return;
    }

    // Hide during setup: the render thread snapshots windows every frame and
    // could pick up this window before AddControl finishes, causing a data race
    // on the controls vector (reallocation while iterating → dangling iterator).
    // Mirror the pattern used in CreateConfigWindow.
    diffWindow->isVisible = false;

    std::weak_ptr<GUIWindow> weakDiffWindow = diffWindow;

    // Title bar — identical graphics and height to GameMenuWindow
    GUIControl titleBar;
    titleBar.type             = GUIControlType::TitleBar;
    titleBar.position         = Vector2(diffWindow->position.x, diffWindow->position.y);
    titleBar.size             = Vector2(diffWindow->size.x, 40);
    titleBar.bgColor          = MyColor(0, 0, 0, 255);
    titleBar.txtColor         = MyColor(255, 255, 0, 255);
    titleBar.bgTextureId      = int(BlitObj2DIndexType::IMG_TITLEBAR2);
    titleBar.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR2);
    titleBar.label            = L"";
    titleBar.lblFontSize      = 18.0f;
    titleBar.isVisible        = true;
    diffWindow->AddControl(titleBar);

    // --- Vertical and horizontal centering for the 5-button cluster ---
    // Cluster total height: 5 buttons × 30px + 4 gaps × 15px = 210px
    const float btnH        = 30.0f;
    const float btnGap      = 15.0f;
    const float startY      = diffWindow->position.y + 55.0f;                                                              // 55px from window top, matching the GameMenu button start position
    const float startX      = diffWindow->position.x + (GAMEMENU_WINDOW_WIDTH - GAMEMENU_BUTTON_WIDTH) * 0.5f;             // Horizontal centre in window (25px)

    // ---------- Button 1 — Easy ----------
    GUIControl easyButton;
    easyButton.type             = GUIControlType::Button;
    easyButton.position         = Vector2(startX, startY);
    easyButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    easyButton.bgColor          = MyColor(0, 0, 0, 255);
    easyButton.txtColor         = MyColor(255, 255, 0, 255);
    easyButton.useShadowedText  = true;
    easyButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    easyButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    easyButton.label            = L"EASY";
    easyButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        easyButton.lblFontSize  = 13.0f;
    #else
        easyButton.lblFontSize  = 16.0f;
    #endif
    easyButton.isVisible = true;

    easyButton.onMouseOver = [weakDiffWindow]() {
        if (auto window = weakDiffWindow.lock()) {
            if (!window->bWindowDestroy) { }
        }
    };
    easyButton.onMouseBtnDown = [this, windowName = std::string(DIFFICULTY_WINDOW_NAME)]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateDifficultyWindow - Easy selected");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            PlayerInfo player;
            player.Difficulty  = DifficultyLevel::DIFFICULTY_EASY;
            player.lives       = 5;
            player.isActive    = true;
            player.isDead      = false;
            player.score       = 0;
            player.health      = 100;
            player.maxHealth   = 100;
            player.armour      = 100;
            player.maxArmour   = 100;
            player.shield      = 100;
            player.maxShield   = 100;
            gamePlayer.InitPlayer(PLAYER_1, player);
            // Fade out GameMenu, then create and fade in GamePlayTypes
            ApplyWindowFadeCallback(
                GUIWindowFadeType::FadeOut, 0.8f, windowName,
                [this, windowName, gptName = std::string(GAMEPLAYTYPES_WINDOW_NAME)]() {
                    RemoveWindow(windowName);
                    CreateGamePlayTypesWindow();
                    ApplyWindowFade(GUIWindowFadeType::FadeIn, 1.0f, gptName);
                }
            );
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateDifficultyWindow - Exception in Easy handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    };
    diffWindow->AddControl(easyButton);

    // ---------- Button 2 — Normal ----------
    GUIControl normalButton;
    normalButton.type             = GUIControlType::Button;
    normalButton.position         = Vector2(startX, startY + (btnH + btnGap));
    normalButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    normalButton.bgColor          = MyColor(0, 0, 0, 255);
    normalButton.txtColor         = MyColor(255, 255, 0, 255);
    normalButton.useShadowedText  = true;
    normalButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    normalButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    normalButton.label            = L"NORMAL";
    normalButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        normalButton.lblFontSize  = 13.0f;
    #else
        normalButton.lblFontSize  = 16.0f;
    #endif
    normalButton.isVisible = true;

    normalButton.onMouseOver = [weakDiffWindow]() {
        if (auto window = weakDiffWindow.lock()) {
            if (!window->bWindowDestroy) { }
        }
    };
    normalButton.onMouseBtnDown = [this, windowName = std::string(DIFFICULTY_WINDOW_NAME)]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateDifficultyWindow - Normal selected");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            PlayerInfo player;
            player.Difficulty  = DifficultyLevel::DIFFICULTY_NORMAL;
            player.lives       = 5;
            player.isActive    = true;
            player.isDead      = false;
            player.score       = 0;
            player.health      = 100;
            player.maxHealth   = 100;
            player.armour      = 100;
            player.maxArmour   = 100;
            player.shield      = 100;
            player.maxShield   = 100;
            gamePlayer.InitPlayer(PLAYER_1, player);
            // Fade out GameMenu, then create and fade in GamePlayTypes
            ApplyWindowFadeCallback(
                GUIWindowFadeType::FadeOut, 0.8f, windowName,
                [this, windowName, gptName = std::string(GAMEPLAYTYPES_WINDOW_NAME)]() {
                    RemoveWindow(windowName);
                    CreateGamePlayTypesWindow();
                    ApplyWindowFade(GUIWindowFadeType::FadeIn, 1.0f, gptName);
                }
            );
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateDifficultyWindow - Exception in Normal handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    };
    diffWindow->AddControl(normalButton);

    // ---------- Button 3 — Hard ----------
    GUIControl hardButton;
    hardButton.type             = GUIControlType::Button;
    hardButton.position         = Vector2(startX, startY + 2.0f * (btnH + btnGap));
    hardButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    hardButton.bgColor          = MyColor(0, 0, 0, 255);
    hardButton.txtColor         = MyColor(255, 255, 0, 255);
    hardButton.useShadowedText  = true;
    hardButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    hardButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    hardButton.label            = L"HARD";
    hardButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        hardButton.lblFontSize  = 13.0f;
    #else
        hardButton.lblFontSize  = 16.0f;
    #endif
    hardButton.isVisible = true;

    hardButton.onMouseOver = [weakDiffWindow]() {
        if (auto window = weakDiffWindow.lock()) {
            if (!window->bWindowDestroy) { }
        }
    };
    hardButton.onMouseBtnDown = [this, windowName = std::string(DIFFICULTY_WINDOW_NAME)]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateDifficultyWindow - Hard selected");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            PlayerInfo player;
            player.Difficulty  = DifficultyLevel::DIFFICULTY_HARD;
            player.lives       = 3;
            player.isActive    = true;
            player.isDead      = false;
            player.score       = 0;
            player.health      = 75;
            player.maxHealth   = 75;
            player.armour      = 75;
            player.maxArmour   = 75;
            player.shield      = 75;
            player.maxShield   = 75;
            gamePlayer.InitPlayer(PLAYER_1, player);
            // Fade out GameMenu, then create and fade in GamePlayTypes
            ApplyWindowFadeCallback(
                GUIWindowFadeType::FadeOut, 0.8f, windowName,
                [this, windowName, gptName = std::string(GAMEPLAYTYPES_WINDOW_NAME)]() {
                    RemoveWindow(windowName);
                    CreateGamePlayTypesWindow();
                    ApplyWindowFade(GUIWindowFadeType::FadeIn, 1.0f, gptName);
                }
            );
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateDifficultyWindow - Exception in Hard handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    };
    diffWindow->AddControl(hardButton);

    // ---------- Button 4 — Very Hard ----------
    GUIControl veryHardButton;
    veryHardButton.type             = GUIControlType::Button;
    veryHardButton.position         = Vector2(startX, startY + 3.0f * (btnH + btnGap));
    veryHardButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    veryHardButton.bgColor          = MyColor(0, 0, 0, 255);
    veryHardButton.txtColor         = MyColor(255, 255, 0, 255);
    veryHardButton.useShadowedText  = true;
    veryHardButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    veryHardButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    veryHardButton.label            = L"VERY HARD";
    veryHardButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        veryHardButton.lblFontSize  = 13.0f;
    #else
        veryHardButton.lblFontSize  = 16.0f;
    #endif
    veryHardButton.isVisible = true;

    veryHardButton.onMouseOver = [weakDiffWindow]() {
        if (auto window = weakDiffWindow.lock()) {
            if (!window->bWindowDestroy) { }
        }
    };
    veryHardButton.onMouseBtnDown = [this, windowName = std::string(DIFFICULTY_WINDOW_NAME)]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateDifficultyWindow - Very Hard selected");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            PlayerInfo player;
            player.Difficulty  = DifficultyLevel::DIFFICULTY_VERYHARD;
            player.lives       = 2;
            player.isActive    = true;
            player.isDead      = false;
            player.score       = 0;
            player.health      = 50;
            player.maxHealth   = 50;
            player.armour      = 50;
            player.maxArmour   = 50;
            player.shield      = 50;
            player.maxShield   = 50;
            gamePlayer.InitPlayer(PLAYER_1, player);
            // Fade out GameMenu, then create and fade in GamePlayTypes
            ApplyWindowFadeCallback(
                GUIWindowFadeType::FadeOut, 0.8f, windowName,
                [this, windowName, gptName = std::string(GAMEPLAYTYPES_WINDOW_NAME)]() {
                    RemoveWindow(windowName);
                    CreateGamePlayTypesWindow();
                    ApplyWindowFade(GUIWindowFadeType::FadeIn, 1.0f, gptName);
                }
            );
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateDifficultyWindow - Exception in Very Hard handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    };
    diffWindow->AddControl(veryHardButton);

    // ---------- Button 5 — Mate! (Hell) ----------
    GUIControl mateButton;
    mateButton.type             = GUIControlType::Button;
    mateButton.position         = Vector2(startX, startY + 4.0f * (btnH + btnGap));
    mateButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    mateButton.bgColor          = MyColor(0, 0, 0, 255);
    mateButton.txtColor         = MyColor(255, 255, 0, 255);
    mateButton.useShadowedText  = true;
    mateButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    mateButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    mateButton.label            = L"MATE! (HELL)";
    mateButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        mateButton.lblFontSize  = 13.0f;
    #else
        mateButton.lblFontSize  = 16.0f;
    #endif
    mateButton.isVisible = true;

    mateButton.onMouseOver = [weakDiffWindow]() {
        if (auto window = weakDiffWindow.lock()) {
            if (!window->bWindowDestroy) { }
        }
    };
    mateButton.onMouseBtnDown = [this, windowName = std::string(DIFFICULTY_WINDOW_NAME)]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateDifficultyWindow - Mate! (Hell) selected");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            PlayerInfo player;
            player.Difficulty  = DifficultyLevel::DIFFICULTY_HELL;
            player.lives       = 1;
            player.isActive    = true;
            player.isDead      = false;
            player.score       = 0;
            player.health      = 25;
            player.maxHealth   = 25;
            player.armour      = 25;
            player.maxArmour   = 25;
            player.shield      = 25;
            player.maxShield   = 25;
            gamePlayer.InitPlayer(PLAYER_1, player);
            // Fade out GameMenu, then create and fade in GamePlayTypes
            ApplyWindowFadeCallback(
                GUIWindowFadeType::FadeOut, 0.8f, windowName,
                [this, windowName, gptName = std::string(GAMEPLAYTYPES_WINDOW_NAME)]() {
                    RemoveWindow(windowName);
                    CreateGamePlayTypesWindow();
                    ApplyWindowFade(GUIWindowFadeType::FadeIn, 1.0f, gptName);
                }
            );
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateDifficultyWindow - Exception in Mate! handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    };
    diffWindow->AddControl(mateButton);

    // All controls added — safe to make visible now (matches CreateConfigWindow pattern).
    diffWindow->isVisible = true;

    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateDifficultyWindow - Difficulty window created successfully with %d controls",
        static_cast<int>(diffWindow->controls.size()));
}

//-------------------------------------------------------------------------------------------------
// CreateGamePlayTypesWindow — full-height side panel matching the GameMenu style.
// Six buttons centred horizontally and vertically; Stage Select is ghosted / inactive.
// Call: guiManager.CreateGamePlayTypesWindow();
//-------------------------------------------------------------------------------------------------
void GUIManager::CreateGamePlayTypesWindow() {
    if (!renderer) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGamePlayTypesWindow - renderer is null, cannot create window");
        return;
    }

    // Create window — same position and dimensions as GameMenuWindow
    CreateMyWindow(
        GAMEPLAYTYPES_WINDOW_NAME,                                                        // Window name
        GUIWindowType::Dialog,                                              // Dialog type (no close chrome)
        Vector2(renderer->iOrigWidth - GAMEMENU_WINDOW_WIDTH, 0),          // Right-side panel, full height
        Vector2(GAMEMENU_WINDOW_WIDTH, renderer->iOrigHeight),             // Full screen height
        MyColor(0, 0, 0, 0),                                               // Transparent background
        int(BlitObj2DIndexType::NONE)                                       // No background texture
    );

    std::shared_ptr<GUIWindow> gptWindow = GetWindow(GAMEPLAYTYPES_WINDOW_NAME);
    if (!gptWindow) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGamePlayTypesWindow - Failed to retrieve window after creation");
        return;
    }

    // Hide during setup — same race guard as CreateConfigWindow / CreateDifficultyWindow.
    gptWindow->isVisible = false;

    std::weak_ptr<GUIWindow> weakGptWindow = gptWindow;

    // Title bar — identical graphics and height to GameMenuWindow
    GUIControl titleBar;
    titleBar.type             = GUIControlType::TitleBar;
    titleBar.position         = Vector2(gptWindow->position.x, gptWindow->position.y);
    titleBar.size             = Vector2(gptWindow->size.x, 40);
    titleBar.bgColor          = MyColor(0, 0, 0, 255);
    titleBar.txtColor         = MyColor(255, 255, 0, 255);
    titleBar.bgTextureId      = int(BlitObj2DIndexType::IMG_TITLEBAR2);
    titleBar.bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR2);
    titleBar.label            = L"";
    titleBar.lblFontSize      = 18.0f;
    titleBar.isVisible        = true;
    gptWindow->AddControl(titleBar);

    // --- Button layout: top-aligned, matching GameMenu/DifficultyWindow style ---
    // 7 buttons × 30px + 6 gaps × 15px = 300px total cluster height.
    // Campaign sits near the top (y+55); Arcade, Time Rush, Cockpit Mode, Random,
    // Stage Select, and 1 Life Mission are all ghosted — unlocked after Campaign completion.
    const float btnH   = 30.0f;
    const float btnGap = 15.0f;
    const float startY = gptWindow->position.y + 55.0f;                                                                    // Near top, matching GameMenu button start position
    const float startX = gptWindow->position.x + (GAMEMENU_WINDOW_WIDTH - GAMEMENU_BUTTON_WIDTH) * 0.5f;                   // Horizontal centre in window (25px)

    // ---------- Button 1 — Campaign (ACTIVE) ----------
    GUIControl campaignButton;
    campaignButton.type             = GUIControlType::Button;
    campaignButton.position         = Vector2(startX, startY);
    campaignButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    campaignButton.bgColor          = MyColor(0, 0, 0, 255);
    campaignButton.txtColor         = MyColor(255, 255, 0, 255);
    campaignButton.useShadowedText  = true;
    campaignButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    campaignButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    campaignButton.label            = L"CAMPAIGN";
    campaignButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        campaignButton.lblFontSize  = 13.0f;
    #else
        campaignButton.lblFontSize  = 16.0f;
    #endif
    campaignButton.isVisible = true;

    campaignButton.onMouseOver = [weakGptWindow]() {
        if (auto window = weakGptWindow.lock()) {
            if (!window->bWindowDestroy) { }
        }
    };
    campaignButton.onMouseBtnDown = [this, windowName = std::string(GAMEPLAYTYPES_WINDOW_NAME)]() {
        try {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGamePlayTypesWindow - Campaign selected");
            soundManager.PlayImmediateSFX(SFX_ID::SFX_VOICE1);
            // Set game play type to Campaign and proceed
            PlayerInfo* player = gamePlayer.GetPlayerInfo(PLAYER_1);
            player->gameMode = GameMode::MODE_CAMPAIGN;
            StartGame();
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"CreateGamePlayTypesWindow - Exception in Campaign handler: %s",
                std::wstring(e.what(), e.what() + strlen(e.what())).c_str());
        }
    };
    gptWindow->AddControl(campaignButton);

    // ---------- Button 2 — Arcade (GHOSTED — unlocks after Campaign completion) ----------
    GUIControl arcadeButton;
    arcadeButton.type             = GUIControlType::Button;
    arcadeButton.position         = Vector2(startX, startY + 1.0f * (btnH + btnGap));
    arcadeButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    arcadeButton.bgColor          = MyColor(0, 0, 0, 255);
    arcadeButton.txtColor         = MyColor(80, 80, 80, 180);                                                               // Ghosted — grey semi-transparent text
    arcadeButton.useShadowedText  = false;                                                                                  // No shadow on ghosted buttons
    arcadeButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    arcadeButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2UP);                                                 // No hover change
    arcadeButton.label            = L"ARCADE";
    arcadeButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        arcadeButton.lblFontSize  = 13.0f;
    #else
        arcadeButton.lblFontSize  = 16.0f;
    #endif
    arcadeButton.isVisible      = true;
    arcadeButton.isClickHandled = false;                                                                                    // Prevent input system consuming this click
    // No handlers — button is inactive until Campaign is completed
    gptWindow->AddControl(arcadeButton);

    // ---------- Button 3 — Time Rush (GHOSTED — unlocks after Campaign completion) ----------
    GUIControl timeRushButton;
    timeRushButton.type             = GUIControlType::Button;
    timeRushButton.position         = Vector2(startX, startY + 2.0f * (btnH + btnGap));
    timeRushButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    timeRushButton.bgColor          = MyColor(0, 0, 0, 255);
    timeRushButton.txtColor         = MyColor(80, 80, 80, 180);
    timeRushButton.useShadowedText  = false;
    timeRushButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    timeRushButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    timeRushButton.label            = L"TIME RUSH";
    timeRushButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        timeRushButton.lblFontSize  = 13.0f;
    #else
        timeRushButton.lblFontSize  = 16.0f;
    #endif
    timeRushButton.isVisible      = true;
    timeRushButton.isClickHandled = false;
    gptWindow->AddControl(timeRushButton);

    // ---------- Button 4 — Cockpit Mode (GHOSTED — unlocks after Campaign completion) ----------
    GUIControl cockpitButton;
    cockpitButton.type             = GUIControlType::Button;
    cockpitButton.position         = Vector2(startX, startY + 3.0f * (btnH + btnGap));
    cockpitButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    cockpitButton.bgColor          = MyColor(0, 0, 0, 255);
    cockpitButton.txtColor         = MyColor(80, 80, 80, 180);
    cockpitButton.useShadowedText  = false;
    cockpitButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    cockpitButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    cockpitButton.label            = L"COCKPIT MODE";
    cockpitButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        cockpitButton.lblFontSize  = 13.0f;
    #else
        cockpitButton.lblFontSize  = 16.0f;
    #endif
    cockpitButton.isVisible      = true;
    cockpitButton.isClickHandled = false;
    gptWindow->AddControl(cockpitButton);

    // ---------- Button 5 — Random (GHOSTED — unlocks after Campaign completion) ----------
    GUIControl randomButton;
    randomButton.type             = GUIControlType::Button;
    randomButton.position         = Vector2(startX, startY + 4.0f * (btnH + btnGap));
    randomButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    randomButton.bgColor          = MyColor(0, 0, 0, 255);
    randomButton.txtColor         = MyColor(80, 80, 80, 180);
    randomButton.useShadowedText  = false;
    randomButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    randomButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    randomButton.label            = L"RANDOM";
    randomButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        randomButton.lblFontSize  = 13.0f;
    #else
        randomButton.lblFontSize  = 16.0f;
    #endif
    randomButton.isVisible      = true;
    randomButton.isClickHandled = false;
    gptWindow->AddControl(randomButton);

    // ---------- Button 6 — Stage Select (GHOSTED — unlocks after Campaign completion) ----------
    GUIControl stageSelectButton;
    stageSelectButton.type             = GUIControlType::Button;
    stageSelectButton.position         = Vector2(startX, startY + 5.0f * (btnH + btnGap));
    stageSelectButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    stageSelectButton.bgColor          = MyColor(0, 0, 0, 255);
    stageSelectButton.txtColor         = MyColor(80, 80, 80, 180);
    stageSelectButton.useShadowedText  = false;
    stageSelectButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    stageSelectButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    stageSelectButton.label            = L"STAGE SELECT";
    stageSelectButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        stageSelectButton.lblFontSize  = 13.0f;
    #else
        stageSelectButton.lblFontSize  = 16.0f;
    #endif
    stageSelectButton.isVisible      = true;
    stageSelectButton.isClickHandled = false;
    gptWindow->AddControl(stageSelectButton);

    // ---------- Button 7 — 1 Life Mission (GHOSTED — unlocks after Campaign completion) ----------
    GUIControl lifeMissionButton;
    lifeMissionButton.type             = GUIControlType::Button;
    lifeMissionButton.position         = Vector2(startX, startY + 6.0f * (btnH + btnGap));
    lifeMissionButton.size             = Vector2(GAMEMENU_BUTTON_WIDTH, btnH);
    lifeMissionButton.bgColor          = MyColor(0, 0, 0, 255);
    lifeMissionButton.txtColor         = MyColor(80, 80, 80, 180);
    lifeMissionButton.useShadowedText  = false;
    lifeMissionButton.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    lifeMissionButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    lifeMissionButton.label            = L"1 LIFE MISSION";
    lifeMissionButton.bold             = true;
    #if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
        lifeMissionButton.lblFontSize  = 13.0f;
    #else
        lifeMissionButton.lblFontSize  = 16.0f;
    #endif
    lifeMissionButton.isVisible      = true;
    lifeMissionButton.isClickHandled = false;
    gptWindow->AddControl(lifeMissionButton);

    // All controls added — safe to make visible now.
    gptWindow->isVisible = true;

    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateGamePlayTypesWindow - Game play types window created successfully with %d controls",
        static_cast<int>(gptWindow->controls.size()));
}

//-------------------------------------------------------------------------------------------------
// CreateQuitConfirmDialog — modal "About to Quit" confirmation window.
// ESC in SCENE_GAMETITLE now shows this instead of shutting down immediately.
// OK  : fade to black → stop music → signal render thread → PostMessage(WM_CLOSE)
// CANCEL : simply close this dialog and return to the game menu.
// This is a CPGE engine-level dialog (no PROJECT_ONLY_CODE guard).
//-------------------------------------------------------------------------------------------------
void GUIManager::CreateQuitConfirmDialog()
{
    if (!renderer) return;

    // Remove a stale copy only when it exists. RemoveWindow logs missing windows as LOG_ERROR.
    if (GetWindow(QUIT_CONFIRM_WINDOW_NAME))
        RemoveWindow(QUIT_CONFIRM_WINDOW_NAME);

    constexpr int dlgW = 520;
    constexpr int dlgH = 150;
    const int dlgX = (renderer->iOrigWidth  - dlgW) / 2;
    const int dlgY = (renderer->iOrigHeight - dlgH) / 2;

    CreateMyWindow(
        QUIT_CONFIRM_WINDOW_NAME,
        GUIWindowType::Dialog,
        Vector2(static_cast<float>(dlgX), static_cast<float>(dlgY)),
        Vector2(static_cast<float>(dlgW), static_cast<float>(dlgH)),
        MyColor(0, 0, 0, 230), // Transparency on background.
        int(BlitObj2DIndexType::NONE)
    );

    auto dlgWin = GetWindow(QUIT_CONFIRM_WINDOW_NAME);
    if (!dlgWin) return;

    dlgWin->isModal   = true;
    dlgWin->isVisible = false;

    // ---- Title bar ----
    GUIControl titleBar;
    titleBar.type                = GUIControlType::TitleBar;
    titleBar.position            = Vector2(static_cast<float>(dlgX), static_cast<float>(dlgY));
    titleBar.size                = Vector2(static_cast<float>(dlgW), TITLEBAR_HEIGHT);
    titleBar.bgColor             = MyColor(0, 0, 0, 255);
    titleBar.txtColor            = MyColor(255, 220, 0, 255);
    titleBar.bgTextureId         = -1;
    titleBar.bgTextureHoverId    = -1;
    titleBar.useGradient         = true;
    titleBar.gradientTopColor    = MyColor(220, 30, 200, 255);
    titleBar.gradientBottomColor = MyColor(25, 0, 50, 255);
    titleBar.label               = L"CONFIRM EXIT";
    titleBar.lblFontSize         = 14.0f;
    titleBar.lblCenterH          = false;
    titleBar.isVisible           = true;
    dlgWin->AddControl(titleBar);

    // ---- Message text area ----
    GUIControl msgText;
    msgText.type             = GUIControlType::TextArea;
    msgText.position         = Vector2(static_cast<float>(dlgX + 14),
                                       static_cast<float>(dlgY) + TITLEBAR_HEIGHT + 10.0f);
    msgText.size             = Vector2(static_cast<float>(dlgW - 28), 38.0f);
    msgText.label            = L"You are about to Quit the Game.";
    msgText.lblFontSize      = 14.0f;
    msgText.txtColor         = MyColor(255, 255, 255, 255);
    msgText.bgColor          = MyColor(0, 0, 0, 0);
    msgText.bgTextureId      = -1;
    msgText.bgTextureHoverId = -1;
    msgText.hoverColor       = MyColor(0, 0, 0, 0);
    msgText.isVisible        = true;
    dlgWin->AddControl(msgText);

    // ---- OK button — near right → full shutdown ----
    GUIControl okBtn;
    okBtn.type             = GUIControlType::Button;
    okBtn.position         = Vector2(static_cast<float>(dlgX + dlgW - 236),
                                     static_cast<float>(dlgY + dlgH - 46));
    okBtn.size             = Vector2(108.0f, 30.0f);
    okBtn.bgColor          = MyColor(0, 0, 0, 128);
    okBtn.txtColor         = MyColor(255, 255, 0, 255);
    okBtn.useShadowedText  = true;
    okBtn.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    okBtn.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    okBtn.label            = L"OK";
    okBtn.lblFontSize      = 14.0f;
    okBtn.bold             = true;
    okBtn.isVisible        = true;

    okBtn.onMouseBtnDown = [this, windowName = std::string(QUIT_CONFIRM_WINDOW_NAME)]() {
        try {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            RemoveWindow(windowName);
            fxManager.FadeOutThenCallback(
                { 0.0f, 0.0f, 0.0f, 1.0f }, 0.8f, 0.06f,
                []() {
                    StopMusicPlayback();
                    threadManager.threadVars.bIsShuttingDown.store(true);
                    #if defined(_WIN32) || defined(_WIN64)
                        std::thread([]() {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            PostMessage(hwnd, WM_CLOSE, 0, 0);
                        }).detach();
                    #else
                        PostQuitMessage(0);
                    #endif
                });
        }
        catch (...) {}
    };
    dlgWin->AddControl(okBtn);

    // ---- Cancel button — near right side → close dialog ----
    GUIControl cancelBtn;
    cancelBtn.type             = GUIControlType::Button;
    cancelBtn.position         = Vector2(static_cast<float>(dlgX + dlgW - 120),
                                         static_cast<float>(dlgY + dlgH - 46));
    cancelBtn.size             = Vector2(108.0f, 30.0f);
    cancelBtn.bgColor          = MyColor(0, 0, 0, 128);
    cancelBtn.txtColor         = MyColor(255, 255, 0, 255);
    cancelBtn.useShadowedText  = true;
    cancelBtn.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    cancelBtn.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    cancelBtn.label            = L"CANCEL";
    cancelBtn.lblFontSize      = 14.0f;
    cancelBtn.bold             = true;
    cancelBtn.isVisible        = true;

    cancelBtn.onMouseBtnDown = [this, windowName = std::string(QUIT_CONFIRM_WINDOW_NAME)]() {
        try {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            RemoveWindow(windowName);
        }
        catch (...) {}
    };
    dlgWin->AddControl(cancelBtn);

    // Safe to show now
    dlgWin->isVisible = true;
}

//-------------------------------------------------------------------------------------------------
// CreateUserProfileWindow — centered modal window for commander & callsign selection.
//
// Layout (600 × 520 px, centered):
//   • 3D drop-shadow effect via dark border window + inner gradient panels
//   • White→black gradient approximated with 5 stacked Panel controls
//   • Title bar "USER PROFILE" + circle-style 'X' close button (top-right)
//   • Single 96×96 portrait image in the top-left of the content area
//   • Horizontal selector scrollbar directly beneath the portrait (96 px wide)
//     spanning commander indices 0–13; snaps to the nearest integer on release
//   • Commander name + rank displayed to the right of the portrait
//   • Callsign TextInput (20 chars, alphanumeric only — SQL-safe)
//   • 7 read-only HSlider attribute bars: Speed, Shields, HULL, CASH, REGEN, WEAPON, CLOAKING
//   • SAVE and CLOSE buttons at bottom-right
//
// On SAVE: writes to UserProfile.dat (binary) and mirrors profileID / playerName /
//          playerExperience into config (saved to GameConfig.cfg with checksum).
//-------------------------------------------------------------------------------------------------
#ifdef PROJECT_ONLY_CODE
void GUIManager::CreateUserProfileWindow()
{
    if (!renderer) return;

    // Remove stale copies only when they exist. RemoveWindow logs missing windows as LOG_ERROR.
    if (GetWindow(USERPROFILE_SHADOW_NAME))
        RemoveWindow(USERPROFILE_SHADOW_NAME);
    if (GetWindow(USERPROFILE_WINDOW_NAME))
        RemoveWindow(USERPROFILE_WINDOW_NAME);

    constexpr int winW = 600;
    constexpr int winH = 535;
    const int winX = (renderer->iOrigWidth  - winW) / 2;
    const int winY = (renderer->iOrigHeight - winH) / 2;

    // ----------------------------------------------------------------
    // Shadow backing window (dark offset rectangle, lower z-order)
    // ----------------------------------------------------------------
    CreateMyWindow(
        USERPROFILE_SHADOW_NAME,
        GUIWindowType::Dialog,
        Vector2(static_cast<float>(winX + 8), static_cast<float>(winY + 8)),
        Vector2(static_cast<float>(winW),      static_cast<float>(winH)),
        MyColor(10, 10, 10, 180),
        int(BlitObj2DIndexType::NONE)
    );
    {
        auto shadow = GetWindow(USERPROFILE_SHADOW_NAME);
        if (shadow) { shadow->isModal = false; }   // visibility set by fade-in below
    }

    // ----------------------------------------------------------------
    // Main UserProfile window (very dark border = drop shadow illusion)
    // ----------------------------------------------------------------
    CreateMyWindow(
        USERPROFILE_WINDOW_NAME,
        GUIWindowType::Dialog,
        Vector2(static_cast<float>(winX), static_cast<float>(winY)),
        Vector2(static_cast<float>(winW), static_cast<float>(winH)),
        MyColor(15, 15, 15, 252),
        int(BlitObj2DIndexType::NONE)
    );

    auto win = GetWindow(USERPROFILE_WINDOW_NAME);
    if (!win) { RemoveWindow(USERPROFILE_SHADOW_NAME); return; }

    win->isModal   = true;
    win->isVisible = false;

    // ----------------------------------------------------------------
    // Main body background — 80% opaque black panel (inset 5px)
    // ----------------------------------------------------------------
    {
        GUIControl bodyBg;
        bodyBg.type             = GUIControlType::Panel;
        bodyBg.position         = Vector2(static_cast<float>(winX + 5), static_cast<float>(winY + 5));
        bodyBg.size             = Vector2(static_cast<float>(winW - 10), static_cast<float>(winH - 10));
        bodyBg.bgColor          = MyColor(0, 0, 0, 204);   // 80 % opaque black
        bodyBg.bgTextureId      = -1;
        bodyBg.bgTextureHoverId = -1;
        bodyBg.sliderValue      = 0.0f;   // flat — no bevel effect
        bodyBg.isVisible        = true;
        win->AddControl(bodyBg);
    }

    // ----------------------------------------------------------------
    // Title bar "USER PROFILE" — mid-grey → black gradient, no hover tint
    // ----------------------------------------------------------------
    constexpr float CLOSE_BTN_SIZE = 20.0f;
    GUIControl titleBar;
    titleBar.type             = GUIControlType::TitleBar;
    titleBar.position         = Vector2(static_cast<float>(winX + 5), static_cast<float>(winY + 2));
    titleBar.size             = Vector2(static_cast<float>(winW - 10), TITLEBAR_HEIGHT);
    titleBar.bgColor          = MyColor(0, 0, 0, 255);
    titleBar.txtColor         = MyColor(220, 220, 220, 255);
    titleBar.bgTextureId      = -1;
    titleBar.bgTextureHoverId = -1;
    titleBar.useGradient      = true;
    titleBar.gradientTopColor    = MyColor(110, 110, 110, 255);   // above-mid-grey
    titleBar.gradientBottomColor = MyColor(0, 0, 0, 255);         // black
    titleBar.label            = L"USER PROFILE";
    titleBar.lblFontSize      = 14.0f;
    titleBar.bold             = true;
    titleBar.lblCenterH       = false;
    titleBar.isVisible        = true;
    win->AddControl(titleBar);

    // ---- Red square close button ('X') — top-right, vertically centred in title bar ----
    GUIControl closeBtn;
    closeBtn.type             = GUIControlType::Button;
    closeBtn.position         = Vector2(
        static_cast<float>(winX + winW - 10) - CLOSE_BTN_SIZE,
        static_cast<float>(winY + 2) + (TITLEBAR_HEIGHT - (CLOSE_BTN_SIZE+2)) * 0.5f);
    closeBtn.size             = Vector2(CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
    closeBtn.bgColor          = MyColor(0, 0, 0, 0);
    closeBtn.hoverColor       = MyColor(180, 0, 0, 160);
    closeBtn.txtColor         = MyColor(255, 200, 200, 230);
    closeBtn.bgTextureId      = -1;
    closeBtn.bgTextureHoverId = -1;
    closeBtn.useCircleShape   = false;
    closeBtn.label            = L"X";
    closeBtn.lblFontSize      = 11.0f;
    closeBtn.bold             = true;
    closeBtn.isVisible        = true;

    closeBtn.onMouseBtnDown = [this]() {
        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
        ApplyWindowFade(GUIWindowFadeType::FadeOut, 0.35f, USERPROFILE_SHADOW_NAME);
        ApplyWindowFadeCallback(GUIWindowFadeType::FadeOut, 0.35f, USERPROFILE_WINDOW_NAME,
            [this]() {
                RemoveWindow(USERPROFILE_WINDOW_NAME);
                RemoveWindow(USERPROFILE_SHADOW_NAME);
            });
    };
    win->AddControl(closeBtn);

    // ----------------------------------------------------------------
    // Portrait image — 96×96, top-left of content area
    // ----------------------------------------------------------------
    const int portX = winX + 10;
    const int portY = winY + 40;
    constexpr int portW = 128, portH = 128;

    const int initProfileID = std::clamp(config.myConfig.profileID, 0, MAX_COMMANDER_PROFILES - 1);

    GUIControl portrait;
    portrait.type             = GUIControlType::Panel;
    portrait.id               = "profile_portrait";
    portrait.position         = Vector2(static_cast<float>(portX), static_cast<float>(portY));
    portrait.size             = Vector2(static_cast<float>(portW), static_cast<float>(portH));
    portrait.bgColor          = MyColor(40, 40, 40, 200);
    portrait.bgTextureId      = kCommanderRoster[initProfileID].portraitTexIndex;
    portrait.bgTextureHoverId = kCommanderRoster[initProfileID].portraitTexIndex;
    portrait.sliderValue      = 0.7f;
    portrait.isVisible        = true;
    win->AddControl(portrait);

    // ----------------------------------------------------------------
    // Profile selector HSlider — 96 px wide, directly under the portrait
    // Spans 0.0 (profile 0) to 13.0 (profile 13).
    // ----------------------------------------------------------------
    const int sliderY = portY + portH + 4;

    GUIControl profSlider;
    profSlider.type             = GUIControlType::HSlider;
    profSlider.id               = "profile_selector";
    profSlider.position         = Vector2(static_cast<float>(portX), static_cast<float>(sliderY));
    profSlider.size             = Vector2(static_cast<float>(portW), 22.0f);
    profSlider.bgColor          = MyColor(50, 50, 50, 220);
    profSlider.hoverColor       = MyColor(70, 70, 70, 220);
    profSlider.sliderMin        = 0.0f;
    profSlider.sliderMax        = static_cast<float>(MAX_COMMANDER_PROFILES - 1);
    profSlider.sliderValue      = static_cast<float>(initProfileID);
    profSlider.isClickHandled   = true;
    profSlider.bgTextureId      = int(BlitObj2DIndexType::IMG_BEVEL1);
    profSlider.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BEVEL1);
    profSlider.isVisible        = true;

    // When dragged, snap to integer profile index and update all dependent controls.
    profSlider.onSliderChanged = [this](float val) {
        const int profileIdx = std::clamp(static_cast<int>(std::round(val)), 0, MAX_COMMANDER_PROFILES - 1);
        auto profileWin = GetWindow(USERPROFILE_WINDOW_NAME);
        if (!profileWin || profileWin->bWindowDestroy) return;

        const CommanderProfile& cmd = kCommanderRoster[profileIdx];
        const int texID = cmd.portraitTexIndex;

        for (auto& c : profileWin->controls) {
            if (c.id == "profile_portrait") {
                c.bgTextureId = texID;
                c.bgTextureHoverId = texID;
            }
            else if (c.id == "cmd_name") {
                c.label = std::wstring(cmd.name, cmd.name + strnlen_s(cmd.name, 24));
            }
            else if (c.id == "cmd_rank") {
                c.label = CommanderRankToString(cmd.rank);
            }
            else if (c.id == "attr_speed")       { c.sliderValue = cmd.stats.speed    / 100.0f; }
            else if (c.id == "attr_shields")      { c.sliderValue = cmd.stats.shields  / 100.0f; }
            else if (c.id == "attr_hull")         { c.sliderValue = cmd.stats.hull     / 100.0f; }
            else if (c.id == "attr_cash")         { c.sliderValue = cmd.stats.cash     / 100.0f; }
            else if (c.id == "attr_regen")        { c.sliderValue = cmd.stats.regen    / 100.0f; }
            else if (c.id == "attr_weapon")       { c.sliderValue = cmd.stats.weapon   / 100.0f; }
            else if (c.id == "attr_cloaking")     { c.sliderValue = cmd.stats.cloaking / 100.0f; }
            else if (c.id == "attr_speed_val")    { c.label = std::to_wstring(cmd.stats.speed)    + L"%"; }
            else if (c.id == "attr_shields_val")  { c.label = std::to_wstring(cmd.stats.shields)  + L"%"; }
            else if (c.id == "attr_hull_val")     { c.label = std::to_wstring(cmd.stats.hull)     + L"%"; }
            else if (c.id == "attr_cash_val")     { c.label = std::to_wstring(cmd.stats.cash)     + L"%"; }
            else if (c.id == "attr_regen_val")    { c.label = std::to_wstring(cmd.stats.regen)    + L"%"; }
            else if (c.id == "attr_weapon_val")   { c.label = std::to_wstring(cmd.stats.weapon)   + L"%"; }
            else if (c.id == "attr_cloaking_val") { c.label = std::to_wstring(cmd.stats.cloaking) + L"%"; }
        }
    };
    win->AddControl(profSlider);

    // ----------------------------------------------------------------
    // Commander name + rank — right of portrait
    // ----------------------------------------------------------------
    const int infoX = portX + portW + 14;
    const int infoY = portY;

    GUIControl cmdNameCtrl;
    cmdNameCtrl.type             = GUIControlType::TextArea;
    cmdNameCtrl.id               = "cmd_name";
    cmdNameCtrl.position         = Vector2(static_cast<float>(infoX), static_cast<float>(infoY));
    cmdNameCtrl.size             = Vector2(static_cast<float>(winX + winW - infoX - 10), 34.0f);
    {
        const CommanderProfile& initCmd = kCommanderRoster[initProfileID];
        cmdNameCtrl.label = std::wstring(initCmd.name, initCmd.name + strnlen_s(initCmd.name, 24));
    }
    cmdNameCtrl.lblFontSize      = 22.0f;
    cmdNameCtrl.bold             = true;
    cmdNameCtrl.txtColor         = MyColor(255, 255, 255, 255);
    cmdNameCtrl.shadowedTxtColor = MyColor(50, 50, 50, 200);
    cmdNameCtrl.useShadowedText  = true;
    cmdNameCtrl.bgColor          = MyColor(0, 0, 0, 0);
    cmdNameCtrl.bgTextureId      = -1;
    cmdNameCtrl.bgTextureHoverId = -1;
    cmdNameCtrl.hoverColor       = MyColor(0, 0, 0, 0);
    cmdNameCtrl.isVisible        = true;
    win->AddControl(cmdNameCtrl);

    GUIControl cmdRankCtrl;
    cmdRankCtrl.type             = GUIControlType::TextArea;
    cmdRankCtrl.id               = "cmd_rank";
    cmdRankCtrl.position         = Vector2(static_cast<float>(infoX), static_cast<float>(infoY + 38));
    cmdRankCtrl.size             = Vector2(static_cast<float>(winX + winW - infoX - 10), 26.0f);
    cmdRankCtrl.label            = CommanderRankToString(kCommanderRoster[initProfileID].rank);
    cmdRankCtrl.lblFontSize      = 16.0f;
    cmdRankCtrl.txtColor         = MyColor(255, 255, 255, 255);
    cmdRankCtrl.shadowedTxtColor = MyColor(50, 50, 50, 200);
    cmdRankCtrl.useShadowedText  = true;
    cmdRankCtrl.bgColor          = MyColor(0, 0, 0, 0);
    cmdRankCtrl.bgTextureId      = -1;
    cmdRankCtrl.bgTextureHoverId = -1;
    cmdRankCtrl.hoverColor       = MyColor(0, 0, 0, 0);
    cmdRankCtrl.isVisible        = true;
    win->AddControl(cmdRankCtrl);

    // ----------------------------------------------------------------
    // Callsign editor
    // ----------------------------------------------------------------
    const int callsignY = sliderY + 28;   // gap below profile selector slider

    GUIControl callsignLabel;
    callsignLabel.type        = GUIControlType::TextArea;
    callsignLabel.position    = Vector2(static_cast<float>(winX + 10), static_cast<float>(callsignY));
    callsignLabel.size        = Vector2(118.0f, 26.0f);
    callsignLabel.label       = L"YOUR CALLSIGN:";
    callsignLabel.lblFontSize      = 11.0f;
    callsignLabel.txtColor         = MyColor(255, 255, 255, 255);
    callsignLabel.shadowedTxtColor = MyColor(50, 50, 50, 200);
    callsignLabel.useShadowedText  = true;
    callsignLabel.bgColor          = MyColor(0, 0, 0, 0);
    callsignLabel.bgTextureId      = -1;
    callsignLabel.bgTextureHoverId = -1;
    callsignLabel.hoverColor       = MyColor(0, 0, 0, 0);
    callsignLabel.isVisible        = true;
    win->AddControl(callsignLabel);

    GUIControl callsignInput;
    callsignInput.type             = GUIControlType::TextInput;
    callsignInput.id               = "callsign_input";
    callsignInput.position         = Vector2(static_cast<float>(winX + 132),
                                             static_cast<float>(callsignY - 1));
    callsignInput.size             = Vector2(static_cast<float>(winW - 142), 26.0f);
    callsignInput.bgColor          = MyColor(230, 230, 230, 255);
    callsignInput.txtColor         = MyColor(255, 215, 0, 255);   // bold gold yellow
    callsignInput.bold             = true;
    callsignInput.bgTextureId      = int(BlitObj2DIndexType::IMG_BEVEL1);
    callsignInput.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BEVEL1);
    callsignInput.maxInputLength   = 20;
    callsignInput.placeholder      = L"Enter callsign (max 20 chars, A-Z 0-9)";
    {
        const std::string& saved = config.myConfig.playerName;
        callsignInput.inputText = std::wstring(saved.begin(), saved.end());
        callsignInput.cursorPos = static_cast<int>(callsignInput.inputText.size());
    }
    callsignInput.lblFontSize = 12.0f;
    callsignInput.isVisible   = true;
    win->AddControl(callsignInput);

    // Filtered keyboard routing: alphanumeric only, no spaces or SQL-unsafe chars, max 20.
    auto weakWin = std::weak_ptr<GUIWindow>(win);
    win->onCharInput = [weakWin](wchar_t ch) {
        auto w = weakWin.lock();
        if (!w) return;
        for (auto& ctrl : w->controls) {
            if (ctrl.type != GUIControlType::TextInput || !ctrl.isFocused) continue;
            if (static_cast<int>(ctrl.inputText.size()) >= ctrl.maxInputLength) return;
            auto uc = static_cast<unsigned char>(ch & 0xFF);
            if (!std::isalnum(uc)) return;   // reject spaces, quotes, SQL chars
            ctrl.inputText.insert(ctrl.inputText.begin() + ctrl.cursorPos,
                                  static_cast<wchar_t>(ch));
            ++ctrl.cursorPos;
            if (ctrl.onTextChanged) ctrl.onTextChanged(ctrl.inputText);
            return;
        }
    };
    win->onBackspace = [weakWin]() {
        auto w = weakWin.lock();
        if (!w) return;
        for (auto& ctrl : w->controls) {
            if (ctrl.type != GUIControlType::TextInput || !ctrl.isFocused) continue;
            if (ctrl.cursorPos > 0 && !ctrl.inputText.empty()) {
                ctrl.inputText.erase(ctrl.cursorPos - 1, 1);
                --ctrl.cursorPos;
                if (ctrl.onTextChanged) ctrl.onTextChanged(ctrl.inputText);
            }
            return;
        }
    };

    // ----------------------------------------------------------------
    // Attribute bars
    // ----------------------------------------------------------------
    const int attrTitleY = callsignY + 46;

    GUIControl attrTitle;
    attrTitle.type        = GUIControlType::TextArea;
    attrTitle.position    = Vector2(static_cast<float>(winX + 10), static_cast<float>(attrTitleY));
    attrTitle.size        = Vector2(static_cast<float>(winW - 20), 28.0f);
    attrTitle.label       = L"COMMANDER ATTRIBUTES:";
    attrTitle.lblFontSize      = 15.0f;
    attrTitle.bold             = true;
    attrTitle.txtColor         = MyColor(255, 255, 255, 255);
    attrTitle.shadowedTxtColor = MyColor(50, 50, 50, 200);
    attrTitle.useShadowedText  = true;
    attrTitle.bgColor          = MyColor(0, 0, 0, 0);
    attrTitle.bgTextureId      = -1;
    attrTitle.bgTextureHoverId = -1;
    attrTitle.hoverColor       = MyColor(0, 0, 0, 0);
    attrTitle.isVisible        = true;
    win->AddControl(attrTitle);

    struct AttrRow { const wchar_t* lbl; const char* sliderID; const char* valID; int initVal; };
    const CommanderStats& s0 = kCommanderRoster[initProfileID].stats;
    const AttrRow attrRows[] = {
        { L"SPEED",    "attr_speed",    "attr_speed_val",    s0.speed    },
        { L"SHIELDS",  "attr_shields",  "attr_shields_val",  s0.shields  },
        { L"HULL",     "attr_hull",     "attr_hull_val",     s0.hull     },
        { L"CASH",     "attr_cash",     "attr_cash_val",     s0.cash     },
        { L"REGEN",    "attr_regen",    "attr_regen_val",    s0.regen    },
        { L"WEAPON",   "attr_weapon",   "attr_weapon_val",   s0.weapon   },
        { L"CLOAKING", "attr_cloaking", "attr_cloaking_val", s0.cloaking },
    };
    constexpr int ATTR_ROWS = 7;
    const float attrStartY = static_cast<float>(attrTitleY + 24);
    const float attrRowH   = 32.0f;
    // Attribute bar layout — use available window width
    const float attrLblX   = static_cast<float>(winX + 10);
    const float attrBarX   = static_cast<float>(winX + 100);
    const float attrBarW   = static_cast<float>(winW - 170);  // leaves 60px for value label
    const float attrValX   = attrBarX + attrBarW + 6.0f;

    for (int ai = 0; ai < ATTR_ROWS; ++ai) {
        const float rowY = attrStartY + ai * attrRowH;

        GUIControl lbl;
        lbl.type             = GUIControlType::TextArea;
        lbl.position         = Vector2(attrLblX, rowY);
        lbl.size             = Vector2(88.0f, 26.0f);
        lbl.label            = attrRows[ai].lbl;
        lbl.lblFontSize      = 12.0f;
        lbl.txtColor         = MyColor(255, 255, 255, 255);
        lbl.shadowedTxtColor = MyColor(50, 50, 50, 200);
        lbl.useShadowedText  = true;
        lbl.bgColor          = MyColor(0, 0, 0, 0);
        lbl.hoverColor       = MyColor(0, 0, 0, 0);
        lbl.bgTextureId      = -1;
        lbl.bgTextureHoverId = -1;
        lbl.isVisible        = true;
        win->AddControl(lbl);

        GUIControl slider;
        slider.type             = GUIControlType::HSlider;
        slider.id               = attrRows[ai].sliderID;
        slider.position         = Vector2(attrBarX, rowY + 2.0f);
        slider.size             = Vector2(attrBarW, 20.0f);
        slider.bgColor          = MyColor(20, 20, 20, 200);
        slider.hoverColor       = MyColor(20, 20, 20, 200);
        slider.sliderMin        = 0.0f;
        slider.sliderMax        = 1.0f;
        slider.sliderValue      = attrRows[ai].initVal / 100.0f;
        slider.isClickHandled   = false;   // display-only
        slider.bgTextureId      = -1;
        slider.bgTextureHoverId = -1;
        slider.drawAsPill       = true;
        slider.pillFillColor    = MyColor(40, 180, 40, 255);
        slider.isVisible        = true;
        win->AddControl(slider);

        GUIControl valLbl;
        valLbl.type             = GUIControlType::TextArea;
        valLbl.id               = attrRows[ai].valID;
        valLbl.position         = Vector2(attrValX, rowY);
        valLbl.size             = Vector2(50.0f, 26.0f);
        valLbl.label            = std::to_wstring(attrRows[ai].initVal) + L"%";
        valLbl.lblFontSize      = 11.0f;
        valLbl.txtColor         = MyColor(255, 255, 255, 255);
        valLbl.shadowedTxtColor = MyColor(50, 50, 50, 200);
        valLbl.useShadowedText  = true;
        valLbl.bgColor          = MyColor(0, 0, 0, 0);
        valLbl.hoverColor       = MyColor(0, 0, 0, 0);
        valLbl.bgTextureId      = -1;
        valLbl.bgTextureHoverId = -1;
        valLbl.isVisible        = true;
        win->AddControl(valLbl);
    }

    // ----------------------------------------------------------------
    // SAVE button
    // ----------------------------------------------------------------
    const int btnY = winY + winH - 44;

    GUIControl saveBtn;
    saveBtn.type             = GUIControlType::Button;
    saveBtn.position         = Vector2(static_cast<float>(winX + winW - 236), static_cast<float>(btnY));
    saveBtn.size             = Vector2(108.0f, 34.0f);
    saveBtn.bgColor          = MyColor(20, 20, 35, 128);   // 50% opaque when idle
    saveBtn.hoverColor       = MyColor(60, 60, 90, 255);
    saveBtn.txtColor         = MyColor(210, 210, 210, 255);
    saveBtn.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    saveBtn.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    saveBtn.label            = L"SAVE";
    saveBtn.lblFontSize      = 14.0f;
    saveBtn.isVisible        = true;

    saveBtn.onMouseBtnDown = [this]() {
        try {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

            auto profileWin = GetWindow(USERPROFILE_WINDOW_NAME);
            if (!profileWin || profileWin->bWindowDestroy) return;

            int          newProfileID = config.myConfig.profileID;
            std::wstring newCallsignW;

            for (auto& c : profileWin->controls) {
                if (c.id == "profile_selector")
                    newProfileID = std::clamp(static_cast<int>(std::round(c.sliderValue)),
                                              0, MAX_COMMANDER_PROFILES - 1);
                if (c.id == "callsign_input")
                    newCallsignW = c.inputText;
            }

            // Sanitise callsign — alphanumeric only (no spaces/SQL-unsafe chars), max 20 chars
            std::string newCallsign;
            newCallsign.reserve(20);
            for (wchar_t wc : newCallsignW) {
                if (newCallsign.size() >= 20) break;
                auto uc = static_cast<unsigned char>(wc & 0xFF);
                if (std::isalnum(uc))
                    newCallsign += static_cast<char>(uc);
            }
            if (newCallsign.empty()) newCallsign = "Commander";

            // Persist to binary profile file
            UserProfileData pdata{};
            strncpy_s(pdata.playerName, sizeof(pdata.playerName), newCallsign.c_str(), _TRUNCATE);
            pdata.current_money = config.myConfig.current_money;
            pdata.profileID     = newProfileID;
            pdata.experience    = config.myConfig.playerExperience;
            pdata.checksum      = ComputeProfileChecksum(pdata);
            SaveUserProfile(pdata);

            // Mirror into config + re-save with checksum
            config.myConfig.profileID        = newProfileID;
            config.myConfig.playerName       = newCallsign;
            config.myConfig.playerExperience = pdata.experience;
            config.saveConfig();

            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"[UserProfile] Saved: callsign='%hs', profileID=%d, xp=%llu",
                newCallsign.c_str(), newProfileID, pdata.experience);

            // Fade out both windows then remove them
            ApplyWindowFade(GUIWindowFadeType::FadeOut, 0.4f, USERPROFILE_SHADOW_NAME);
            ApplyWindowFadeCallback(GUIWindowFadeType::FadeOut, 0.4f, USERPROFILE_WINDOW_NAME,
                [this]() {
                    RemoveWindow(USERPROFILE_WINDOW_NAME);
                    RemoveWindow(USERPROFILE_SHADOW_NAME);
                });
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR,
                L"[UserProfile] Exception in save handler: %hs", e.what());
        }
    };
    win->AddControl(saveBtn);

    // ---- CLOSE button ----
    GUIControl closeWinBtn;
    closeWinBtn.type             = GUIControlType::Button;
    closeWinBtn.position         = Vector2(static_cast<float>(winX + winW - 120),
                                           static_cast<float>(btnY));
    closeWinBtn.size             = Vector2(108.0f, 34.0f);
    closeWinBtn.bgColor          = MyColor(20, 20, 35, 128);   // 50% opaque when idle
    closeWinBtn.hoverColor       = MyColor(60, 60, 90, 255);
    closeWinBtn.txtColor         = MyColor(210, 210, 210, 255);
    closeWinBtn.bgTextureId      = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    closeWinBtn.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    closeWinBtn.label            = L"CLOSE";
    closeWinBtn.lblFontSize      = 14.0f;
    closeWinBtn.isVisible        = true;

    closeWinBtn.onMouseBtnDown = [this]() {
        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
        ApplyWindowFade(GUIWindowFadeType::FadeOut, 0.35f, USERPROFILE_SHADOW_NAME);
        ApplyWindowFadeCallback(GUIWindowFadeType::FadeOut, 0.35f, USERPROFILE_WINDOW_NAME,
            [this]() {
                RemoveWindow(USERPROFILE_WINDOW_NAME);
                RemoveWindow(USERPROFILE_SHADOW_NAME);
            });
    };
    win->AddControl(closeWinBtn);

    // Fade both windows in from transparent so the window appears to materialise.
    ApplyWindowFade(GUIWindowFadeType::FadeIn, 0.35f, USERPROFILE_SHADOW_NAME);
    ApplyWindowFade(GUIWindowFadeType::FadeIn, 0.35f, USERPROFILE_WINDOW_NAME);

    debug.logDebugMessage(LogLevel::LOG_INFO, L"CreateUserProfileWindow - created with %d controls",
        static_cast<int>(win->controls.size()));
}
#endif // PROJECT_ONLY_CODE
