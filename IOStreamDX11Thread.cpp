/* ---------------------------------------------------------------- 
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ---------------------------------------------------------------- 
This is the placement code for DX11Renderer Loader Thread.

This where you are to load & initialise all necessary resources 
for the given scene.
----------------------------------------------------------------- */
#include "Includes.h"

#if defined(__USE_DIRECTX_11__)

#include "MathPrecalculation.h"
#include "ExceptionHandler.h"
#include "ThreadManager.h"
#include "WinSystem.h"
#include "ShaderManager.h"
#include "Models.h"
#include "Lights.h"
#include "SoundManager.h"
#include "SceneManager.h"
#include "GUIManager.h"
#include "DX_FXManager.h"
#include "DX11Renderer.h"

using namespace SoundSystem;

// Other required external references.
extern ExceptionHandler exceptionHandler;
extern ThreadManager threadManager;
extern SystemUtils sysUtils;
extern SceneManager scene;
extern ShaderManager shaderManager;
extern SoundManager soundManager;
extern GUIManager guiManager;
extern FXManager fxManager;
extern Model models[MAX_MODELS];
extern LightsManager lightsManager;
extern WindowMetrics winMetrics;
extern bool bResizing;
extern int textScrollerEffectID;
extern bool Load_Music();													// Function in main.cpp to load music for the game

std::mutex DX11Renderer::s_loaderMutex;

/* -------------------------------------------------------------- */
// Main Tasking Thread for our I/O Loader Tasking Service
/* -------------------------------------------------------------- */
void DX11Renderer::LoaderTaskThread()
{
	exceptionHandler.RecordFunctionCall("LoaderTaskThread");
	std::lock_guard<std::mutex> lock(s_loaderMutex);

    // Check the status of the Loader thread
    ThreadStatus status = threadManager.GetThreadStatus(THREAD_LOADER);
	// State that we have loading to do....
	threadManager.threadVars.bLoaderTaskFinished.store(false);

	while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
		  (status != ThreadStatus::Terminated) && (!threadManager.threadVars.bIsShuttingDown.load()) && (status != ThreadStatus::Stopped))
    {
		sysUtils.ProcessMessages();
		status = threadManager.GetThreadStatus(THREAD_LOADER);
		if (status == ThreadStatus::Paused)
        {
			// Pause and then recheck
            Sleep(100);
            continue;
        }

		// (Add I/O loading tasks here)
		switch (scene.stSceneType)
		{
			case SceneType::SCENE_SPLASH:
			{
				threadManager.threadVars.b2DTexturesLoaded.store(false);
				if (LoadAllKnownTextures())
					// State that we have loade all our required 2D Textures.
					threadManager.threadVars.b2DTexturesLoaded.store(true);

				threadManager.PauseThread(THREAD_LOADER);
				threadManager.threadVars.bLoaderTaskFinished.store(true);
				break;
			}

			case SceneType::SCENE_INTRO:
			{
				threadManager.threadVars.b2DTexturesLoaded.store(false);
				debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene Intro.");
				if (LoadAllKnownTextures())
					// State that we have loade all our required 2D Textures.
					threadManager.threadVars.b2DTexturesLoaded.store(true);

				// If we are NOT resizing our window, then ....
				if (!wasResizing.load())
				{
                    Load_Music();

					// Create Game Menu
					myCamera.SetupDefaultCamera(static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight));
					guiManager.CreateGameMenuWindow(L"");

					// Create a starfield with 100 stars, a radius of 1000, and reset distance of 1000
					fxManager.CreateStarfield(100, 1000.0f, 1000.0f);
					fxManager.FadeToImage(1.0f, 0.08f);

					std::wstring newsText = L"BREAKING NEWS: [16/06/2025] => This is a demonstration of the CPGE GLTF 2.0 Animation System in Action!";
					XMFLOAT4 textColor(0.0f, 1.0f, 0.0f, 1.0f);                     // Green text color
				
					float fontSize = 16.0f;                                         // Font size for text
					float regionX = -5.0f;                                          // X position of scroll region
					float regionY = float(iOrigHeight) - 100.0f;                    // Y position of scroll region (top of screen)
					float regionWidth = float(iOrigWidth) + 10.0f;                  // Width of scroll region (full screen width)
					float regionHeight = 25.0f;                                     // Height of scroll region
					float scrollSpeed = 60.0f;                                      // Pixels per second scroll speed
					float duration = FLT_MAX;                                       // Infinite duration (continuous)

					// Before calling CreateTextScrollerLTOR, get the next ID that will be assigned
					textScrollerEffectID = static_cast<int>(fxManager.effects.size()) + 1;
					fxManager.CreateTextScrollerConsistent(newsText, L"MayaCulpa", fontSize, textColor,
						regionX, regionY, regionWidth, regionHeight,
						scrollSpeed, duration);
				}

				// This must go at the end, so critical rendering can start
				threadManager.PauseThread(THREAD_LOADER);
				threadManager.threadVars.bLoaderTaskFinished.store(true);
				break;
			}

			case SceneType::SCENE_LOAD_MP3:
			{
				try {
					#if defined(__USE_MP3PLAYER__)
						CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);					// Must Initialize as STA (Single Threaded)
						player.Initialize(hwnd);
						auto fileName = AssetsDir / SingleMP3Filename;
						if (player.loadFile(fileName)) {
							debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: MP3 File Re-loaded successfully.");
							player.play();
							player.fadeIn(5000);
						}
						else {
							debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LOADER]: Failed to load file.");
						}
					#endif

					// IMPORTANT: This must be called as the MediaPlayer will need 
					// ========== to process messages before playing!
					sysUtils.GetMessageAndProcess();
				}
				catch (const std::exception& e) {
                    exceptionHandler.LogException(e, "[LOADER THREAD] SceneType::SCENE_LOAD_MP3");
					debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LOADER]: Exception: " + std::wstring(e.what(), e.what() + strlen(e.what())));
				}

				threadManager.PauseThread(THREAD_LOADER);
				threadManager.threadVars.bLoaderTaskFinished.store(true);
				// Make sure we release this regardless.
				CoUninitialize();											// Uninitialize COM
				break;
			}

			case SceneType::SCENE_GAMEPLAY:
			{
				debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene GAMEPLAY Initialising.");
				threadManager.threadVars.b2DTexturesLoaded.store(false);

				if (LoadAllKnownTextures())
					// State that we have loade all our required 2D Textures.
					threadManager.threadVars.b2DTexturesLoaded.store(true);

				/*			// Load in our required 3D textures
							for (int i = 0; i < MAX_TEXTURE_BUFFERS; i++)
							{
								auto fileName = AssetsDir / tex3DFilename[i];
								// Load the texture
								if (!LoadTexture(i, tex3DFilename[i], false))
								{
									std::wstring msg = L"[LOADER]: Failed to load 3D Texture: " + tex3DFilename[i];
									debug.logLevelMessage(LogLevel::LOG_ERROR, msg);
								}
							}
				*/

				// Create a default light	
				LightStruct sunLight;
				SecureZeroMemory(&sunLight, sizeof(LightStruct));		// Clear the light struct
				sunLight.active = true;                                 // Light is active
				sunLight.position = XMFLOAT3(3.0f, -3.0f, -100.0f);     // Above and in front of model
				sunLight.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);       // Shining toward models
				sunLight.color = XMFLOAT3(1.0f, 1.0f, 1.0f);            // White light
				sunLight.ambient = XMFLOAT3(0.4f, 0.4f, 0.0f);          // Base ambient
				sunLight.intensity = 0.3f;                              // Slightly bright
				sunLight.baseIntensity = 0.7f;
				sunLight.Shiningness = 0.0f;                            // How much light is cast
				sunLight.Reflection = 0.0f;                             // How much light is reflected
				sunLight.lightFalloff = 0.0001f;                        // How much light diminishes over distance
				sunLight.innerCone = 30.0f;							    // Inner cone angle
				sunLight.outerCone = 60.0f;							    // Outer cone angle
				sunLight.range = 1000.0f;
//				sunLight.type = int(LightType::POINT);
				sunLight.type = int(LightType::DIRECTIONAL);

				lightsManager.CreateLight(L"Sun", sunLight);

				// -----------------------------------------------------------------------------
				// ====----- THIS BLOCK WILL BE REPLACED WITH PROPER SCENE MANAGEMENT -----=====
				// -----------------------------------------------------------------------------
				// Load in our required models and Initialize	
				// -----------------------------------------------------------------------------
				scene.ParseGLBScene(AssetsDir / L"test-anim1.glb");
				if (!scene.bGltfCameraParsed)
				{
					scene.AutoFrameSceneToCamera();
				}
				// Create animation instance first
				int parentID = scene.FindParentModelID(L"Icosphere");
				bool created = scene.gltfAnimator.CreateAnimationInstance(parentID, 0);
				if (created) {
					// Configure animation settings
					scene.gltfAnimator.SetAnimationSpeed(parentID, 0.25f);						    // Half speed
					scene.gltfAnimator.SetAnimationLooping(parentID, true);						// Enable looping
					scene.gltfAnimator.StartAnimation(parentID, 0);
				}

//				scene.ParseGLTFScene(AssetsDir / L"scene1.gltf");

				try 
				{
					// If we are NOT resizing our window, then ....
					if (!wasResizing.load())
					{
						Load_Music();

						// IMPORTANT: This must be called as the MediaPlayer will need 
						// ========== to process messages before starting playback!
						sysUtils.ProcessMessages();

					} // End of Resizing check
				}
				catch (const std::exception& e) {
					exceptionHandler.LogException(e, "[LOADER THREAD] SceneType::GAMEPLAY");
					threadManager.threadVars.bLoaderTaskFinished.store(true);
					threadManager.PauseThread(THREAD_LOADER);
					debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LOADER]: Exception: " + std::wstring(e.what(), e.what() + strlen(e.what())));
				}

				threadManager.threadVars.bLoaderTaskFinished.store(true);
				threadManager.PauseThread(THREAD_LOADER);
				break;
			}

			case SceneType::SCENE_GAMEOVER:
			{
				debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene Game Over.");
				threadManager.threadVars.bLoaderTaskFinished.store(true);
				break;
			}

			default:
			{
				threadManager.threadVars.bLoaderTaskFinished.store(true);
				break;
			}
		} // End of switch (scene.stSceneType)
    }

	// Reset Resize State Flag
	if (!threadManager.threadVars.bIsShuttingDown.load())
	   wasResizing.store(false);

	debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene Loading Complete - Pausing Thread");
}
#endif // __USE_DIRECTX11__