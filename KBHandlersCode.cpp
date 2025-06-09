// ---------------------------------------------------------------------------------------
// KBHandlersCode.cpp
// ==================
// This code manages keyboard input handling for the game engine that interfaces
// to the KeyboardHandler class. It sets up key up handlers for specific keys
// and segments of the game, such as exiting the game when the Escape key is pressed.
// or when the arrow keys are pressed to navigate through the GAMEPLAY scene.
// ---------------------------------------------------------------------------------------
#include "Includes.h"

#include "Debug.h"
#include "ExceptionHandler.h"
#include "SoundManager.h"
#include "SceneManager.h"
#include "DX_FXManager.h"
#include "KeyboardHandler.h"
#include "GamePlayer.h"
#include "GamingAI.h"
#include "ThreadManager.h"

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

void SetMyKeyUpHandler(KeyboardHandler& keyboard)
{ 
    // Register key up handler for release events
    keyboard.SetKeyUpHandler([](KeyCode keyCode, uint32_t modifierFlags) 
    {
        switch (keyCode) 
        {
            case KeyCode::KEY_ESCAPE:
                fxManager.FadeToBlack(1.0f, 0.03f);
                soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                while (fxManager.IsFadeActive()) 
                {
                    #if !defined(RENDERER_IS_THREAD)
                        renderer->RenderFrame();
                    #endif
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }

                threadManager.threadVars.bIsShuttingDown.store(true);
                renderer->WaitToFinishThenPauseThread();
                PostQuitMessage(0);
                break;

                // Toggle wireframe mode with F2 key
                case KeyCode::KEY_F2:
                {
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                            // Toggle wireframe state
                            renderer->bWireframeMode = !renderer->bWireframeMode; 
                    }

                    break;
                }

                // Move to opposite side with F9 key
                case KeyCode::KEY_F9:
                {
                    // Key Releases for when in GAMEPLAY scene
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                            renderer->myCamera.RotateToOppositeSide(2);  // Rotate camera to opposite side
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
                                renderer->myCamera.StopRotating();      // Stop current rotation
                            }
                            else
                            {
                                renderer->myCamera.SetTarget(XMFLOAT3(0.0f, 0.0f, 0.0f)); // Set target to origin
                                renderer->myCamera.MoveAroundTarget(false, true, false, 20.0f, true); // Start rotation
                            }
                    }

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
