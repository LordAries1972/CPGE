// ---------------------------------------------------------------------------------------
// KBHandlersCode.cpp
// ==================
// This code manages keyboard input handling for the game engine that interfaces
// to the KeyboardHandler class. It sets up key up handlers for specific keys
// and segments of the game, such as exiting the game when the Escape key is pressed.
// or when the arrow keys are pressed to navigate through the GAMEPLAY scene.
// ---------------------------------------------------------------------------------------
#include "Includes.h"

#include "Configuration.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "SoundManager.h"
#include "SceneManager.h"
#include "FXManager.h"
#include "KeyboardHandler.h"
#include "GamePlayer.h"
#include "GamingAI.h"
#include "ThreadManager.h"
#include "ScreenRecorder.h"
#include "GUIManager.h"
#include "ConsoleWindow.h"

#if defined(PLATFORM_WINDOWS)
    #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
        #include "DXCamera.h"
    #endif
#elif defined(PLATFORM_LINUX)
#elif defined(PLATFORM_MACOS)
#elif defined(PLATFORM_ANDROID)
#elif defined(PLATFORM_IOS)
#endif

using namespace SoundSystem;

class SceneManager;

extern Debug debug;
extern ExceptionHandler exceptionHandler;
extern FXManager fxManager;
extern GamePlayer gamePlayer;
extern GamingAI gamingAI;
extern SoundManager soundManager;
extern ThreadManager threadManager;
extern SceneManager scene;

// Abstract Renderer Pointer
extern std::shared_ptr<Renderer> renderer;

extern void SwitchToGameIntro();
extern void StopMusicPlayback();
extern std::atomic<bool> bDismissAllSettingOSDs;
extern std::atomic<bool> bRecordingToggleRequested;
extern std::atomic<int>  micVolumeAdjustRequest;
extern std::atomic<int>  musicVolumeAdjustRequest;
extern std::atomic<int>  sfxVolumeAdjustRequest;
extern std::atomic<int>  masterVolumeAdjustRequest;
extern std::atomic<int>  ttsVolumeAdjustRequest;
extern ScreenRecorder    screenRecorder;
extern GUIManager        guiManager;
extern ConsoleWindow     consoleWindow;

// Application window handle — used to gate the recording toggle to our own window.
// The WH_KEYBOARD_LL / CGEventTap hooks fire for all system key presses; without
// this guard, pressing Home in any other app starts the recorder unexpectedly.
#if defined(PLATFORM_WINDOWS)
    extern HWND hwnd;
#endif

void SetMyKeyUpHandler(KeyboardHandler& keyboard)
{ 
    // Register key up handler for release events
    keyboard.SetKeyUpHandler([](KeyCode keyCode, uint32_t modifierFlags) 
    {
        switch (keyCode) 
        {
            case KeyCode::KEY_ESCAPE:
                // Config window takes priority — ESC fires the Close button (cancel + revert)
                if (auto cfgWin = guiManager.GetWindow("ConfigWindow")) {
                    for (auto& ctrl : cfgWin->controls)
                        if (ctrl.id == "btn_close" && ctrl.onMouseBtnDown) {
                            ctrl.onMouseBtnDown();
                            break;
                        }
                    break;
                }
                switch (scene.stSceneType)
                {
                    case SceneType::SCENE_GAMEPLAY:
                    {
                        fxManager.FadeToBlack(1.0f, 0.06f);
                        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                        while (fxManager.IsFadeActive()) 
                        {
                            #if !defined(RENDERER_IS_THREAD)
                                renderer->RenderFrame();
                            #endif
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }

                        StopMusicPlayback();
                        SwitchToGameIntro(); // Switch to game intro scene
                        break;
                    }
                    
                    #if defined(_DEBUG)
                    case SceneType::SCENE_EXPERIMENT:
                    {
                        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

                        // Fade to black — covers tunnel while it is still running.
                        fxManager.FadeToBlack(1.0f, 0.06f);
                        while (fxManager.IsFadeActive())
                        {
                            #if !defined(RENDERER_IS_THREAD)
                                renderer->RenderFrame();
                            #endif
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }

                        // Screen fully black — stop tunnel and discard saved snapshot.
                        // SwitchToGameIntro() will do a full scene reload via the loader
                        // thread, which recreates all GAMETITLE effects (starfield etc.)
                        // from scratch, so RestoreFXAfterScene() is not needed.
                        fxManager.StopAllFX();
                        fxManager.DiscardSavedFXState();
                        SwitchToGameIntro();
                        break;
                    }
                    #endif

                    case SceneType::SCENE_GAMETITLE:
                    {
                        // If the quit-confirm dialog is already open, close it (ESC = cancel).
                        auto qcWin = guiManager.GetWindow("QuitConfirmDialog");
                        if (qcWin && !qcWin->bWindowDestroy) {
                            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                            guiManager.RemoveWindow("QuitConfirmDialog");
                            break;
                        }

                        // If the GamePlayTypes or Difficulty selection window is open, ESC navigates
                        // back to the Game Menu so the user can abort or correct their selection.
                        auto gptWin  = guiManager.GetWindow("GamePlayTypes");
                        auto diffWin = guiManager.GetWindow("DifficultyWindow");

                        if ((gptWin  && !gptWin->bWindowDestroy) ||
                            (diffWin && !diffWin->bWindowDestroy))
                        {
                            // Determine which sub-window is active (GamePlayTypes takes priority)
                            std::string activeWin = (gptWin && !gptWin->bWindowDestroy)
                                                      ? "GamePlayTypes"
                                                      : "DifficultyWindow";

                            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);

                            // Fade out the active sub-window, then recreate the Game Menu
                            guiManager.ApplyWindowFadeCallback(
                                GUIWindowFadeType::FadeOut, 0.8f, activeWin,
                                [activeWin]() {
                                    guiManager.RemoveWindow(activeWin);
                                    guiManager.CreateGameMenuWindow(L"winGameMenu");
                                    guiManager.ApplyWindowFade(GUIWindowFadeType::FadeIn, 1.0f, "GameMenuWindow");
                                }
                            );
                            break;
                        }

                        // No sub-window open — show the quit-confirmation modal instead of
                        // exiting immediately.  OK in the dialog performs the actual shutdown.
                        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                        guiManager.CreateQuitConfirmDialog();
                        break;
                    }
                } // End of switch (keyCode)

                break;

                // Toggle debug info overlay with F2 key (persisted to config, active in all scenes)
                case KeyCode::KEY_F2:
                {
                    config.myConfig.showDebugInfo = !config.myConfig.showDebugInfo;
                    config.saveConfig();
                    renderer->bDebugOSDActive   = true;
                    renderer->debugOSDStartTime = std::chrono::steady_clock::now();
                    bDismissAllSettingOSDs      = true;

                    break;
                }

                // Toggle console output window with F8 key (GAMETITLE / GAMEPLAY only)
                case KeyCode::KEY_F8:
                {
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMETITLE:
                        case SceneType::SCENE_GAMEPLAY:
                            consoleWindow.Toggle();
                            break;
                    }
                    break;
                }

                // Scroll console window up/down with PAGE UP / PAGE DOWN
                case KeyCode::KEY_PAGE_UP:
                {
                    if (consoleWindow.bIsVisible)
                        consoleWindow.Scroll(8);
                    break;
                }

                case KeyCode::KEY_PAGE_DOWN:
                {
                    if (consoleWindow.bIsVisible)
                        consoleWindow.Scroll(-8);
                    break;
                }

                case KeyCode::KEY_BACKSPACE:
                {
                    guiManager.HandleBackspace();
                    break;
                }

                case KeyCode::KEY_DELETE:
                {
                    guiManager.HandleDelete();
                    break;
                }

                case KeyCode::KEY_ENTER:
                {
                    if (consoleWindow.bIsVisible)
                        guiManager.HandleEnter();
                    break;
                }

                // Move to opposite side with F9 key
                case KeyCode::KEY_F9:
                {
                    // Key Releases for when in GAMEPLAY scene
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                            renderer->myCamera.RotateToOppositeSide(2);                                 // Rotate camera to opposite side
                    }

                    break;
                }

                // F12 key-up: gameplay keeps the existing wireframe toggle; in
                // debug title-screen builds it toggles renderer timing capture and
                // dumps the last 25 render-frame samples when turned off.
                case KeyCode::KEY_F12:
                {
                    switch (scene.stSceneType)
                    {
                        #if defined(_DEBUG)
                        case SceneType::SCENE_GAMETITLE:
                            if (renderer)
                            {
                                renderer->ToggleTimingCapture();
                                bDismissAllSettingOSDs = true;
                            }
                            break;
                        #endif

                        case SceneType::SCENE_GAMEPLAY:
                            renderer->bWireframeMode = !renderer->bWireframeMode;
                            break;

                        default:
                            break;
                    }

                    break;
                }

                // Toggle rotation around target with NUMPAD0 key
                case KeyCode::KEY_NUMPAD_0:
                {
                    // Key Releases for when in GAMEPLAY scene
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                            if (renderer->myCamera.IsRotatingAroundTarget())
                            {
                                renderer->myCamera.StopRotating();                                      // Stop current rotation
                            }
                            else
                            {
#if defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
                                renderer->myCamera.SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));               // Set target to origin
#else
                                renderer->myCamera.SetTarget(XMFLOAT3(0.0f, 0.0f, 0.0f));               // Set target to origin
#endif
                                renderer->myCamera.MoveAroundTarget(false, true, false, 20.0f, true);   // Start rotation
                            }
                    }

                    break;
                }

                case KeyCode::KEY_NUMPAD_8:
                {
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                        {
                            // Safe camera jump with renderer validation
                            if (renderer && renderer->bIsInitialized.load() &&
                                !threadManager.threadVars.bIsResizing.load()) {
                                renderer->myCamera.JumpToWithYawPitch(0.0f, 8.0f, 85.0f, 3.14f, 0, 1, true);            // Camera position 1
                            }
                            break;
                        }
                    }
                    break;
                }

                case KeyCode::KEY_NUMPAD_2:
                {
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                        {
                            // Safe camera jump with renderer validation
                            if (renderer && renderer->bIsInitialized.load() &&
                                !threadManager.threadVars.bIsResizing.load()) {
                                renderer->myCamera.JumpToWithYawPitch(0.14f, 8.11f, -69.0f, 0.004f, 0.025f, 1, true);   // Camera position 2
                            }
                            break;
                        }
                    }
                    break;
                }

                case KeyCode::KEY_NUMPAD_4:
                {
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                        {
                            // Safe camera jump with renderer validation
                            if (renderer && renderer->bIsInitialized.load() &&
                                !threadManager.threadVars.bIsResizing.load()) {
                                renderer->myCamera.JumpToWithYawPitch(-90.0f, 0.0f, 5.0f, 0, 0, 1, true);               // Camera position 3
                            }
                            break;
                        }
                    }
                    break;
                }

                case KeyCode::KEY_NUMPAD_6:
                {
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                        {
                            // Safe camera jump with renderer validation
                            if (renderer && renderer->bIsInitialized.load() &&
                                !threadManager.threadVars.bIsResizing.load()) {
                                renderer->myCamera.JumpToWithYawPitch(90.0f, 0.0f, 5.0f, -1.66f, 0.089f, 1, true);      // Camera position 4
                            }
                            break;
                        }
                    }
                    break;
                }
                // NUMPAD+/- volume dispatch (checked most-specific modifier first):
                //   CTRL+ALT   → TTS volume (Windows only)
                //   CTRL+SHIFT → master (system) volume
                //   CTRL       → SFX (dialog) volume
                //   ALT        → music volume
                //   plain      → mic monitor (only while recording)
                case KeyCode::KEY_NUMPAD_ADD:
                {
                    if ((modifierFlags & 0x05) == 0x05)
                        ttsVolumeAdjustRequest.fetch_add(1);
                    else if ((modifierFlags & 0x03) == 0x03)
                        masterVolumeAdjustRequest.fetch_add(1);
                    else if (modifierFlags & 0x01)
                        sfxVolumeAdjustRequest.fetch_add(1);
                    else if (modifierFlags & 0x04)
                        musicVolumeAdjustRequest.fetch_add(1);
                    else if (screenRecorder.IsRecording())
                        micVolumeAdjustRequest.fetch_add(1);
                    break;
                }

                case KeyCode::KEY_NUMPAD_SUBTRACT:
                {
                    if ((modifierFlags & 0x05) == 0x05)
                        ttsVolumeAdjustRequest.fetch_add(-1);
                    else if ((modifierFlags & 0x03) == 0x03)
                        masterVolumeAdjustRequest.fetch_add(-1);
                    else if (modifierFlags & 0x01)
                        sfxVolumeAdjustRequest.fetch_add(-1);
                    else if (modifierFlags & 0x04)
                        musicVolumeAdjustRequest.fetch_add(-1);
                    else if (screenRecorder.IsRecording())
                        micVolumeAdjustRequest.fetch_add(-1);
                    break;
                }

                // Toggle screen recording with HOME key.
                // Only set a flag here — actual MF initialisation happens in the main
                // game loop so the WH_KEYBOARD_LL hook returns immediately and never hangs.
                // Guard with a foreground-window check: the low-level hook fires for
                // ALL system key presses, so pressing Home in another application or
                // on the desktop would start the recorder unexpectedly, which can corrupt
                // the MF pipeline state and cause system instability / shutdowns.
                case KeyCode::KEY_HOME:
                {
                    #if defined(PLATFORM_WINDOWS)
                        if (GetForegroundWindow() == hwnd)
                            bRecordingToggleRequested.store(true);
                    #else
                        bRecordingToggleRequested.store(true);
                    #endif
                    break;
                }

        } // End of switch (keyCode)

        // Collect AI input data if monitoring is active
        switch (scene.stSceneType)
        {
            case SceneType::SCENE_GAMEPLAY:
                // Collect AI input data if monitoring is active
                if (gamingAI.IsMonitoring()) {
                    gamingAI.CollectInputEventData(INPUT_TYPE_KEYBOARD, static_cast<uint32_t>(keyCode));
                }
                break;
        } // End of switch (sceneManager.stSceneType)

        }); // End of SetKeyUpHandler
}
