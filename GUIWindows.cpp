#include "DX11Renderer.h"
#include "RendererMacros.h"
#include "SoundManager.h"
#include "GUIManager.h"
#include "WinSystem.h"
#include "Debug.h"

extern Vector2 myMouseCoords;
extern SoundManager soundManager;
extern WindowMetrics winMetrics;

void GUIManager::CreateAlertWindow(const std::wstring& message) {
    const std::string WINDOW_NAME = "AlertWindow";
    // Create the Alert Window and pass the stored renderer
    CreateMyWindow(
        WINDOW_NAME,                                        // Window name
        GUIWindowType::Alert,                               // Window type (Alert)
        Vector2(200, 150),                                  // Position (x, y)
        Vector2(400, 300),                                  // Size (width, height)
        MyColor(120, 0, 0, 0),                              // Background color (very dark red)
        int(BlitObj2DIndexType::IMG_WINFRAME1)              // Background texture ID
    );

    // Get the created window
    std::shared_ptr<GUIWindow> alertWindow = GetWindow(WINDOW_NAME);
    if (!alertWindow) return;

    // References that are used to pass into events when fired.
    std::weak_ptr<GUIWindow> weakWindow = alertWindow;

    // Add a Title Bar control to the window
    auto titleBar = std::make_shared<GUIControl>();                                       // Use shared_ptr to avoid dangling reference
    titleBar->type = GUIControlType::TitleBar;
    titleBar->position = Vector2(alertWindow->position.x, alertWindow->position.y);       // Position at top of window
    titleBar->size = Vector2(alertWindow->size.x - (CLOSEWINBUTTON_SIZE + 6), TITLEBAR_HEIGHT); // Height of 24 pixels
    titleBar->bgColor = MyColor(0, 0, 0, 255);                                           
    titleBar->txtColor = MyColor(255, 255, 0, 255);
    titleBar->bgTextureId = int(BlitObj2DIndexType::IMG_TITLEBAR1);
    titleBar->bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR1HL);
    titleBar->label = L"   Alert Status!";                                                // Title text
    titleBar->lblFontSize = 18.0f;
    titleBar->isVisible = true;

    // Capture shared_ptr directly (be careful of circular references)
    std::shared_ptr<GUIControl> strongTitleBar = titleBar;
    titleBar->onMouseBtnDown = [strongTitleBar]() {
        if (strongTitleBar->isClickHandled)
        {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Mouse OnMouseDown detected!");
            strongTitleBar->isPressed = true;
            strongTitleBar->isClickHandled = false;
        }
    };

    // Capture shared_ptr directly (be careful of circular references)
    std::shared_ptr<GUIControl> strongTitleBar2 = titleBar;
    std::shared_ptr<GUIWindow> strongWindow = alertWindow;
    strongTitleBar2->onMouseBtnUp = [strongTitleBar2, strongWindow]() {
        if (!strongTitleBar2->isClickHandled)
        {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Mouse OnMouseUp detected!");
            strongTitleBar2->isPressed = false;
            strongTitleBar2->isClickHandled = true;
            strongWindow->isDragging = false;
        }
    };

    // Capture shared_ptr directly (be careful of circular references)
    titleBar->onMouseMove = [strongWindow]() {
        // Handle title bar click (for dragging the window)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MouseMove detected!");
        strongWindow->isDragging = true;
    };

    // Add the control to the window
    alertWindow->AddControl(*titleBar);

    // Set the content text
    alertWindow->contentText = message;

    // Add a Text Area control
    GUIControl textArea;
    textArea.type = GUIControlType::TextArea;
    textArea.position = Vector2(alertWindow->position.x + 6, alertWindow->position.y + (titleBar->size.y + 6));     // Position inside the window
    textArea.size = Vector2(alertWindow->size.x-6 - (SCROLLBAR_WIDTH - 2), alertWindow->size.y - 74);               // Size of the text area
    textArea.lblFontSize = 14.0f;
    textArea.bgColor = MyColor(60, 0, 0, 255);
    textArea.txtColor = MyColor(0, 175, 255, 255);                                                                  // Text color
    textArea.bgTextureId = int(BlitObj2DIndexType::IMG_BEVEL1);
    textArea.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BEVEL1);
    textArea.isVisible = true;
    alertWindow->AddControl(textArea);

/*    // Add a Scrollbar control
    GUIControl scrollbar;
    scrollbar.type = GUIControlType::Scrollbar;
    scrollbar.position = Vector2(alertWindow->position.x + (titleBar->size.x - SCROLLBAR_WIDTH), 
                                 alertWindow->position.y + titleBar->size.y);                                       // Position inside the window
    scrollbar.size = Vector2(SCROLLBAR_WIDTH, alertWindow->size.y - titleBar->size.y);                              // Size of the scrollbar
    scrollbar.lblFontSize = 10.0f;
    scrollbar.bgColor = MyColor(200, 200, 200, 255);                                                                // Scrollbar color (light gray)
    scrollbar.isVisible = true;
    scrollbar.onScroll = [](int position) {
        // Handle scroll event
        printf("Scroll position: %d\n", position);
    };
    alertWindow->AddControl(scrollbar);
    // Calculate the scrollbar range (optional, if you have content)
    alertWindow->CalculateScrollbarRange(scrollbar.lblFontSize);
*/
    // Add an Okay Button control
    GUIControl okayButton;
    okayButton.type = GUIControlType::Button;
    okayButton.position = Vector2(alertWindow->position.x + (140 - winMetrics.borderWidth), 
                                 (alertWindow->position.y + alertWindow->size.y) - 35);                     // Position inside the window
    okayButton.size = Vector2(BUTTON_WIDTH, 30);                                                            // Size of the button
    okayButton.bgColor = MyColor(0, 0, 0, 255);
    okayButton.txtColor = MyColor(0, 80, 255, 255);                                                         // Button color (blue)
    okayButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    okayButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTONUP1);
    okayButton.label = L"Ok";                                                                               // Button label
    okayButton.lblFontSize = 16.0f;
    okayButton.isVisible = true;
    okayButton.onMouseBtnDown = [this, WINDOW_NAME]() {
        // Handle Okay button click event
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Button was clicked!");
        SoundSystem::SoundQueueItem immediateItem;
//        soundManager.AddToQueue(SFX_ID::SFX_BEEP, PlaybackType::pbtSFX_PlayOnce,
//            PriorityType::ptPLAY_IMMEDIATELY, StereoBalance::BALANCE_CENTER, 1.0f, 2.0f);

        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
        RemoveWindow(WINDOW_NAME);                                                                          // Close the Alert Window
    };
    alertWindow->AddControl(okayButton);

    // Add a Close Button control
    GUIControl btnClose;
    btnClose.type = GUIControlType::Button;
    btnClose.position = Vector2((alertWindow->position.x + alertWindow->size.x) -
                                (CLOSEWINBUTTON_SIZE + 4), alertWindow->position.y + 4);                    // Position at the top-right corner
    btnClose.size = Vector2(CLOSEWINBUTTON_SIZE, CLOSEWINBUTTON_SIZE);                                      // Size of the close button
    btnClose.bgColor = MyColor(120, 0, 0, 255);                                                             // Red color
    btnClose.txtColor = MyColor(80, 0, 0, 255);                                                             // Red color
    btnClose.bgTextureId = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
    btnClose.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BTNCLOSEUP1);
    btnClose.label = L"";                                                                                   // Close button label
    btnClose.lblFontSize = 8.0f;
    btnClose.isVisible = true;
    btnClose.onMouseBtnDown = [this, WINDOW_NAME]() 
    {
        // Handle close button click event
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Window Close Button was clicked!\n");
        SoundSystem::SoundQueueItem immediateItem;
        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
        RemoveWindow(WINDOW_NAME);                                                                           // Close the window
    };

    alertWindow->AddControl(btnClose);
}

void GUIManager::CreateGameMenuWindow(const std::wstring& message) {
    const std::string WINDOW_NAME = "GameMenuWindow";
    WithDX11Renderer([this, WINDOW_NAME](std::shared_ptr<DX11Renderer> dx11)
    {
            // Create the Alert Window and pass the stored renderer
            CreateMyWindow(
                WINDOW_NAME,                                                                                 // Window name
                GUIWindowType::Dialog,                                                                       // Window type (Dialog)
                Vector2(dx11->iOrigWidth - 318, 0),                                                          // Position (x, y)
                Vector2(300, dx11->iOrigHeight),                                                             // Size (width, height)
                MyColor(0, 0, 0, 0),                                                                         // Background color (black, overlayed with images)
                int(BlitObj2DIndexType::NONE)                                                                // No Background texture ID
            );
    });

    // Get the created window
    std::shared_ptr<GUIWindow> gameMenuWindow = GetWindow(WINDOW_NAME);
    if (!gameMenuWindow) return;

    // References that are used to pass into events when fired.
    std::weak_ptr<GUIWindow> weakWindow = gameMenuWindow;

    // Add a Title Bar control to the window
    auto titleBar = std::make_shared<GUIControl>();                                                          // Use shared_ptr to avoid dangling reference
    titleBar->type = GUIControlType::TitleBar;
    titleBar->position = Vector2(gameMenuWindow->position.x, gameMenuWindow->position.y);                    // Position at top of window
    titleBar->size = Vector2(gameMenuWindow->size.x, 40);                                                    // Height of 40 pixels
    titleBar->bgColor = MyColor(0, 0, 0, 255);
    titleBar->txtColor = MyColor(255, 255, 0, 255);
    titleBar->bgTextureId = int(BlitObj2DIndexType::IMG_TITLEBAR2);
    titleBar->bgTextureHoverId = int(BlitObj2DIndexType::IMG_TITLEBAR2);
    titleBar->label = L"";                                                                                   // Title text
    titleBar->lblFontSize = 18.0f;
    titleBar->isVisible = true;
    // Add the control to the window
    gameMenuWindow->AddControl(*titleBar);

    // Add an Configuration Button control
    GUIControl configButton;
    configButton.type = GUIControlType::Button;
    configButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 55);        // Position inside the window
    configButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Size of the button
    configButton.bgColor = MyColor(0, 0, 0, 255);
    configButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Text Clour (Yellow)
    configButton.useShadowedText = true;
    configButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    configButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    configButton.label = L"      Configuration";                                                               // Button label
    configButton.lblFontSize = 16.0f;
    configButton.isVisible = true;
    configButton.onMouseBtnDown = [this, WINDOW_NAME]()
    {
        // Handle Okay button click event
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Button was clicked!");
        SoundSystem::SoundQueueItem immediateItem;
        //        soundManager.AddToQueue(SFX_ID::SFX_BEEP, PlaybackType::pbtSFX_PlayOnce,
        //            PriorityType::ptPLAY_IMMEDIATELY, StereoBalance::BALANCE_CENTER, 1.0f, 2.0f);

        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
//        RemoveWindow(WINDOW_NAME);                                                                            // Close the Alert Window
    };

    gameMenuWindow->AddControl(configButton);

    // Add an Game Play Button control
    GUIControl gameplayButton;
    gameplayButton.type = GUIControlType::Button;
    gameplayButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 110);       // Position inside the window
    gameplayButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Size of the button
    gameplayButton.bgColor = MyColor(0, 0, 0, 255);
    gameplayButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Text Clour (Yellow)
    gameplayButton.useShadowedText = true;
    gameplayButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    gameplayButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    gameplayButton.label = L"        Game Play";                                                                 // Button label
    gameplayButton.lblFontSize = 16.0f;
    gameplayButton.isVisible = true;
    gameplayButton.onMouseBtnDown = [this, WINDOW_NAME]()
    {
        // Handle Okay button click event
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Button was clicked!");
        SoundSystem::SoundQueueItem immediateItem;
        //        soundManager.AddToQueue(SFX_ID::SFX_BEEP, PlaybackType::pbtSFX_PlayOnce,
        //            PriorityType::ptPLAY_IMMEDIATELY, StereoBalance::BALANCE_CENTER, 1.0f, 2.0f);

        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
//            RemoveWindow(WINDOW_NAME);                                                                            // Close the Alert Window
    };

    gameMenuWindow->AddControl(gameplayButton);

    // Add an Game Play Button control
    GUIControl hiscoresButton;
    hiscoresButton.type = GUIControlType::Button;
    hiscoresButton.position = Vector2(gameMenuWindow->position.x + 25, gameMenuWindow->position.y + 165);       // Position inside the window
    hiscoresButton.size = Vector2(GAMEMENU_BUTTON_WIDTH, 30);                                                   // Size of the button
    hiscoresButton.bgColor = MyColor(0, 0, 0, 255);
    hiscoresButton.txtColor = MyColor(255, 255, 0, 255);                                                        // Text Clour (Yellow)
    hiscoresButton.useShadowedText = true;
    hiscoresButton.bgTextureId = int(BlitObj2DIndexType::IMG_BUTTON2UP);
    hiscoresButton.bgTextureHoverId = int(BlitObj2DIndexType::IMG_BUTTON2DOWN);
    hiscoresButton.label = L"      High Scores Table";                                                          // Button label
    hiscoresButton.lblFontSize = 16.0f;
    hiscoresButton.isVisible = true;
    hiscoresButton.onMouseBtnDown = [this, WINDOW_NAME]()
        {
            // Handle Okay button click event
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Button was clicked!");
            SoundSystem::SoundQueueItem immediateItem;
            //        soundManager.AddToQueue(SFX_ID::SFX_BEEP, PlaybackType::pbtSFX_PlayOnce,
            //            PriorityType::ptPLAY_IMMEDIATELY, StereoBalance::BALANCE_CENTER, 1.0f, 2.0f);

            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
            //            RemoveWindow(WINDOW_NAME);                                                                            // Close the Alert Window
        };

    gameMenuWindow->AddControl(hiscoresButton);
}
