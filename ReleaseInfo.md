# DirectX 11 & 12, OpenGL & Vulkan Game Engine

**Cross Platform Gaming Engine by Daniel J. Hobson**  
*Melbourne, Australia 2023-2026*

*Current Build Version: v0.0.1201*

---

Before we start, if you need some catching up, now that we have our website available, 

please visit https://ultimanium.com/index.php?action=cpge for current details! (NOTE: Please do not use Proxy deferrer or VPN!)

We will be having our linkable demos on this page, so please stay tuned as many will be released soon to demonstrate
the engines full power!  I am planning to make the CPGE engine a flight engine upon our new game coming soon called,
"The Shadows of Orion".  This game will demonstrate to people that:-

1) This Gaming Engine will cut it and give you the needs to build a fantastic game across multiple platforms for both 2D and 3D environments!
2) The Engine does what it states it does at current release (Unless I am not aware of a bug)!
3) FOSS (Free and Open Source Software) which means, you can use this system under MIT license without worries, 
   just only to give due credit where needed on our use of system in your game - Come on, give credit where credit is due 
   and support us!  Thats it!

4) Versatile and complex systems integrated to get ya feet off the ground in regards to those designated 
   areas!  (ie, that be Effects, Renderer, Sounds, Music, Networking etc).
   
5) Tools will be coming, its part of the plan to help and assist your gaming development.
6) Scripting Support will be coming also real soon! - As of 27th April, 2026 (A favourite development of mine!)
7) Why are you paying people like UNREAL engine? your adding cost to your game, which means you must ask the higher price, why? - Time to stop this BS right!
8) Even thou the system is not currently as advanced as UNREAL and the others out there, why help them to only elevate your costs right!
9) Time to jump on board, making a game is what we do, and contribute your recommendations here so we can all 
   benefit from the expertise of others and yet, keep our costs down for our players so we all can make a buck right!
10) Scripting support that allows the user to customise scene generation via scripts for those who are NOT programmable savvy or for those who can
    manipulate sequencing without the code in the engine for the scene transitions. This allows a direct API interface to the main core engine and
    that been said, allows you do externally edit your game engine.

It should also be duely noted, that we will NEVER EVER charge anyone for the privilege of using our gaming engine!

To me, its only common sense, join now our community, request a PULL Request from management on our website as given above, so you too can help the community
build a real gaming platform on Multiple-Platform Systems and yet, keep our gaming prices low so that users can afford to play!

To me, economically and the logic, only seems right! - So please get on board and thank you for taking the time to
visit this page today!

I thank personally, everyone who is willing and sacrifices time and contributes, to show me support - respect to you all and 
lets make this Engine great!

## Table of Contents

### [License Information](#license-information)
### [Project Overview](#project-overview)
### [Release Status](#release-status)

### Development History by Month and Year

#### 2026

- [May 2026](#may-2026---more-major-updates-and-fixes)
- [April 2026](#april-2026---bug-fixes-and-updates)

#### 2025
- July 2025 -> Took time here to work on some of my Blender Add-ons so the 3D work can start.

- [June 2025](#june-2025---ai-and-data-systems)
- [May 2025](#may-2025---advanced-features)
- [April 2025](#april-2025---3d-rendering-breakthrough)
- [March 2025](#march-2025---core-development)
- [January 2025](#january-2025---major-restructuring-start)

#### 2024
- June 2024 => Dec 2024 - Initial Project started, various modules where created, but with not fully carefully thinking out the development plan, I decided to do a clean restart of the project again, whilst maintaining what I had already developed and could be used.

#### 2023
- [September 2023](#september-2023---project-foundation)

### [Future Development](#future-development)
### [Technical Requirements](#technical-requirements)

---

## License Information

This software system is licensed under the MIT License.  
Full license details available in: [`Docs\LICENSE.md`](Docs/LICENSE.md)

## Project Overview

The **Cross Platform Gaming Engine (CPGE)** is designed for cross-platform functionality, targeting gaming and high-performance rendering systems. The engine supports:

- **Graphics Features:**
  - Textures, lighting, shading, blending
  - Shadowing and specular effects
  - Multiple rendering backends (DirectX 11/12, OpenGL, Vulkan)

- **Input Systems:**
  - Keyboard and joystick handling
  - Gamepad support with custom mappings

- **Audio Systems:**
  - Sound FX (SoundManager)
  - Multi-format music system (.MP3 and .XM Fast Tracker 2)

- **Development Features:**
  - Extensive debugging options
  - Comprehensive logging system
  - Hot-reloading capabilities

**Technical Standard:** C++17 Compliant

## Release Status

Once the base DirectX 11 implementation is complete, the project will be released to the open community for contribution, growth, and public use.

---

## Development History

### September 2023 - Project Foundation
**September 11, 2023** - Original Conceptional Idea and Project started with focus on building just core interface class modules.

### January 2025 - Major Restructuring Start

**January 1, 2025** - Complete project restart with improved architecture and efficiency  
**January 7, 2025** - XM Module Player and Class implementation began  

### March 2025 - Core Development

**March 11, 2025** - Debug Class implementation for Visual Studio 2019/2022 output window integration

**March 12, 2025** - ThreadManager Class creation for safe thread management  
*See: [`ThreadManager.h/.cpp`](ThreadManager.h)*

**March 14, 2025** - DirectX Camera class object implementation  
*See: [`DXCamera.h/.cpp`](DXCamera.h)*

**March 15, 2025** - Core rendering functionality achieved:
- FPS display and mouse cursor rendering
- Loader Thread (LoaderTaskThread) startup, processing, and closure
- Render Thread (RenderFrame) startup, processing, and closure
- 2D texture loading implementation
- Custom mouse pointer functionality
- Basic DirectDraw pipeline operational
- Basic scene functionality operational
- 2D object blitting with transparency
- SteamWorks Class & SDK integration (not yet implemented)

**March 16, 2025** - Audio and input systems:
- Full functional MP3 Player (threaded) using WindowsMedia API  
  *See: [`WinMediaPlayer.h`](WinMediaPlayer.h)*
- Complete joystick/gamepad handler with mapping save/load functionality
- Optional RenderFrame() threading or main loop execution
- SysUtils class for Windows-specific operations  
  *See: [`WinSystem.h`](WinSystem.h)*
- Windows 10+ system requirements enforcement

**March 17, 2025** - GPU detection implementation

**March 18, 2025** - MP3Player enhancements:
- Playlist support
- Auto-restart for single song playback

**March 21, 2025** - ThreadManager Class improvements  
*See: [`ThreadManager.h`](ThreadManager.h)*

**March 25, 2025** - GUIManager system implementation:
- Custom window creation with control support
- Input handling and event management
- 2D draw/image support

**March 26, 2025** - GUIManager enhancements  
*See: [`GUIManager.h/.cpp`](GUIManager.h)*

**March 27, 2025** - Documentation improvements with usage examples in individual .cpp files

**March 28, 2025** - SoundManager system implementation  
*See: [`SoundManager.h/.cpp`](SoundManager.h)*

**March 29, 2025** - FX Manager implementation with screen fading routines  
*See: [`FXManager.h/.cpp`](FXManager.h)*

**March 30, 2025** - FX Manager scrolling effects for tiled images

### April 2025 - 3D Rendering Breakthrough

**April 1, 2025** - Major restructuring milestone:
1. **XMMODPlayer Class** - Fully functional DirectX Sound playback  
	*See: [`XMMODPlayer.h/.cpp`](XMMODPlayer.h), [`Docs/XMMODPlayer-Example-Usage.md`](Docs/XMMODPlayer-Example-Usage.md)*
2. **Cross-platform preparation** - Major restructuring for multi-platform support

**April 3, 2025** - Rendering system simplification:
- Enhanced renderer abstraction  
  *See: [`main.cpp`](main.cpp), [`Renderer.h`](Renderer.h), [`RendererMacros.h`](RendererMacros.h), [`RendererFactory.cpp`](RendererFactory.cpp)*
- SoundManager rework: XAudio2 → DirectSound transition

**April 4, 2025** - Core functionality improvements:
- Full window resizing implementation
- XMMODPlayer Class enhancements

**April 5, 2025** - Documentation expansion in [`/Docs`](Docs/) folder

**April 9, 2025** - 3D rendering breakthrough:
- 3D OBJ model rendering with textures, normal maps, lighting, and shading
- Manual model loading for educational purposes.
- Pixel shader debugging capabilities  
  *See: [`ModelPixel.hlsl`](Assets/Shaders/ModelPixel.hlsl) and [`RenderFrame()'] in (DXRenderFrame.cpp)*
- Multi-GPU support with NVIDIA/AMD priority
- Complete camera system with mouse and wheel controls
- Basic model animation implementation
- Atomic operations corrections

**April 15, 2025** - Wireframe mode toggle (F2 key)

**April 16, 2025** - Scene Manager development (75% complete)  
*GLTF/.bin 2.0 format support*

**April 17, 2025** - Debug system enhancements:
- `logLevelMessage()` and `logDebugMessage()` file output
- Date/time stamping implementation  
*See: [`Debug.h/.cpp`](Debug.h)*

**April 18, 2025** - Camera system overhaul:
- Complete DXCamera rework  
*See: [`DXCamera.h/.cpp`](DXCamera.h)*
- Cursor key navigation implementation
- Mouse right-button yaw/pitch control
- Mouse wheel zoom functionality

**April 24, 2025** - **STABLE RELEASE**: Basic GLTF 2.0 scene model parsing and rendering
- Functional wireframe mode
- Pixel shader optimization pending

**April 26, 2025** - **STABLE RELEASE**: GLTF 2.0 Parser optimization:
- Full functionality achievement
- Basic lighting support  
*See: [`SceneManager.h/.cpp`](SceneManager.h), [`Models.h/.cpp`](Models.h)*

**April 29, 2025** - Window management improvements:
- Resize functionality
- Scene data reloading/re-initialization
- Clean exit implementation

**April 30, 2025** - **STABLE RELEASE**: Enhanced user experience:
- Camera position preservation during resize
- Loader-ring animation bug fixes

### May 2025 - Advanced Features

**May 5, 2025** - Sketchfab integration and effects:
- Sketchfab GLTF parsing support
- 2D Particle explosion (fireworks) effects in FXManager  
*See: [`DX_FXManager.h/.cpp`](DX_FXManager.h)*

**May 12, 2025** - **100% FUNCTIONAL**: Gamepad/Joystick integration:
- Complete 2D and 3D scenario support
- Camera movement integration

**May 16, 2025** - **STABLE RELEASE**: Thread-safe video rendering:
- .AVI/.MP4 file support
- Video streaming without sound (by design)
- External audio track overlay capability  
*See: [`MoviePlayer.h/.cpp`](MoviePlayer.h)*

**May 17, 2025** - Scene sequencing implementation:
- Splash screen → Intro movie → Game intro flow
- 3D Starfield effect in FXManager

**May 18, 2025** - Display management:
- Exclusive fullscreen implementation
- Scene-specific rendering stages (FUSI - Fully Utilize Scene Implementation)

**May 21, 2025** - ThreadManager locking enhancements:
- Lock/Unlock functionality
- CheckLock/TryLock functions  
*See: [`ThreadManager.h/.cpp`](ThreadManager.h)*

**May 23, 2025** - Text effects and mathematics:
- FXManager text scroller routines (LTOR, RTOL, Consistent, Movie)  
*See: [`DX_FXManager.h/.cpp`](DX_FXManager.h), [`Docs/FXManager-TextScrollers-Example-Usage.md`](Docs/FXManager-TextScrollers-Example-Usage.md)*
- MathPrecalculation Class introduction  
*See: [`MathPrecalculation.h/.cpp`](MathPrecalculation.h), [`Docs/MathPrecalculation-Example-Usage.md`](Docs/MathPrecalculation-Example-Usage.md)*

**May 25, 2025** - Fullscreen mode completion:
- Screen mode selection (width, height)
- Smooth transitions
- Default RELEASE build behavior
- Threaded renderer stability improvements

**May 27, 2025** - **[IN PROGRESS]**: Multi-renderer integration:
- OpenGL & DirectX 12 implementation analysis
- Cross-platform renderer support architecture
- Runtime renderer selection capability
- Future: Vulkan & Android support

**May 30, 2025** - Platform verification and TTS implementation:
- DirectX11 renderer compilation verification
- TTSManager (Text To Speech) implementation  
*See: [`TTSManager.h/.cpp`](TTSManager.h), [`Docs/TTSManager-Example-Usage.md`](Docs/TTSManager-Example-Usage.md)*

**May 31, 2025** - Network infrastructure:
- Threaded TCP/UDP Network Manager for Windows  
*See: [`NetworkManager.h/.cpp`](NetworkManager.h), [`Docs/NetworkManager-Example-Usage.md`](Docs/NetworkManager-Example-Usage.md)*

### June 2025 - AI and Data Systems

**June 1, 2025** - Data management and player systems:
- **PUNPack Class**: Compression/decompression with checksum support  
*See: [`PUNPack.h/.cpp`](PUNPack.h), [`Docs/PUNPack-Example-Usage.md`](Docs/PUNPack-Example-Usage.md)*
- **GamePlayer, GameAccount & GameStatus Classes**: Complete player management  
*See: [`GamePlayer.h/.cpp`](GamePlayer.h), [`Docs/GamePlayer-Example-Usage.md`](Docs/GamePlayer-Example-Usage.md)*

**June 2, 2025** - AI system implementation:
- **GamingAI Class**: Player strategy learning and adaptation  
*See: [`GamingAI.h/.cpp`](GamingAI.h), [`Docs/GamingAI-Example-Usage.md`](Docs/GamingAI-Example-Usage.md)*

**June 3, 2025** - File operations and random generation:
- **Thread-Safe FileIO Class**: Cross-platform file operations  
*See: [`FileIO.h/.cpp`](FileIO.h), [`Docs/FileIO-Example-Usage.md`](Docs/FileIO-Example-Usage.md)*
- **MyRandomizer Class**: Gaming-focused random number generation  
*See: [`MyRandomizer.h/.cpp`](MyRandomizer.h), [`Docs/MyRandomizer-Example-Usage.md`](Docs/MyRandomizer-Example-Usage.md)*

**June 5, 2025** - Exception handling:
- **ExceptionHandler Class**: Comprehensive exception capture and logging  
*See: [`ExceptionHandler.h/.cpp`](ExceptionHandler.h), [`Docs/ExceptionHandler-Example-Usage.md`](Docs/ExceptionHandler-Example-Usage.md)*

**June 6, 2025** - **STABLE**: Shader management system:
- **ShaderManager Class**: Multi-platform shader support (HLSL, GLSL, SPIR-V)  
*See: [`ShaderManager.h/.cpp`](ShaderManager.h), [`Docs/ShaderManager-Example-Usage.md`](Docs/ShaderManager-Example-Usage.md)*
- Models.cpp and main.cpp integration
- RenderFrame() relocated to seperate file [`DXRenderFrame.cpp`](DXRenderFrame.cpp) as all master rendering should be place here.

**June 7, 2025** - **STABLE**: Cleaning up and Documentation Conversion to Mark Down
- **ShaderManager Class**: Some basic rework and removed non-needed functionality for Windows Platform.  
- Fixed a couple of bugs/reworked a couple of things in main.cpp file.
- Existing Documents in [`Docs/`](/Docs) have been converted to Mark Down format for easier reading and referencing via GitHUB.
- Created Example usage file for [`ThreadManager`](Docs/ThreadManager-Example-Usage.md)
- Created Example usage file for [`SoundManager`](Docs/SoundManager-Example-Usage.md)
- Created Example usage file for [`XMMODPlayer`](Docs/XMMODPlayer-Example-Usage.md)
- Created Example usage file for [`ExceptionHandler`](Docs/ExceptionHandler-Example-Usage.md)
- Created Example usage file for [`Joystick`](Docs/Joystick-Example-Usage.md)
- Created Example usage file for [`Lighting`](Docs/Light-Example-Usage.md)
- Created Example usage file for [`Models`](Docs/Model-Example-Usage.md)

**June 8, 2025** - **STABLE**: DirectX Camera Management System:
- **Camera Class**: Implemented Camera Animation, Jump & Rotate functions with options for speed & target focusing.
                    Which is now also compliant for both DirectX 11 & 12 platforms.
- *See: [`DXCamera.h/.cpp`](DXCamera.h), [`Docs/DXCamera-Example-Usage.md`](Docs/DXCamera-Example-Usage.md)*

- **KeyboardHandler Class**: Implemented Keyboard Handler Class that supports various functionality for all supporting OS Platforms.
- *See: [`KeyboardHandler.h/.cpp`](KeyboardHandler.h), [`Docs/KeyboardHandler-Example-Usage.md`](Docs/KeyboardHandler-Example-Usage.md)*

**June 09-10, 2025** - Implementation: Comprehensive Physics Class Implementation:

- Physics Class: Complete physics simulation system with 10 major physics laws implemented
- MathPrecalculation Integration: Added 8 new physics optimization methods with lookup tables
- Performance Optimization: 6 new lookup tables for gravity, reflection, inertia, collision, audio, and orbital mechanics
- Cross-Platform Compatibility: Full C++17 compliant implementation for all target platforms
- Some bug fixes for the DXCamera
- *See: [`Physics.h/.cpp`](Physics.h), [`Docs/Physics-Example-Usage.md`](Docs/Physics-Example-Usage.md)*

**June 15, 2025** - Implementation: SceneManager Enhancements:

- Implemented ParseGLBScene() which now imports GLB 2.0 format files.
- GLTFAnimator Class: Complete Animation system that are parsed via ParseGLTFScene() or ParseGLBScene() in SceneManager class.
- Models now have an assigned Parent ID if model is of a Child Model Object.
- Created the SceneManager & Animator Documentation.
- *See: [`GLTFAnimator.h/.cpp`](GLTFAnimator.h), [`Docs/SceneManager-Example-Usage.md`](Docs/SceneManager-Example-Usage.md)*

**June 17, 2025** - Bug Fixes & Corrections: SceneManager & GLTFAnimator:

- Fixed various issues in the Scene Managers ParseGLTFScene() & ParseGLBScene() functionality for diverse compatibility with Blender v4.4
- GLTFAnimator Class: Corrected various problems in regards to Animations, playback is now smooth and with looping.
- Models now have their true name assigned from the GLTF/GLB file, if one does not exist, then its given a generated name.
- Updated the SceneManager & Animator Documentation.
- *See: [`GLTFAnimator.h/.cpp`](GLTFAnimator.h), [`Docs/SceneManager-Example-Usage.md`](Docs/SceneManager-Example-Usage.md)*

### April 2026 - Bug fixes and Updates

**April 18, 2026** - Bug Fixes & Corrections: SceneManager, GLTFAnimator and Screen & Audio Recorder:

- Fixed various issues in the Scene Managers ParseGLTFScene() & ParseGLBScene() functionality for diverse compatibility from version 4 and including up-to with Blender v5.1
- Model animation playback is now working as expected.
- ScreenRecorder class introduced which captures every frame and audio to a .mp4 file (HOME key toggles on/off functionality).
- Created Example usage file for [`ScreenRecorder`](Docs/ScreenRecorder-Example-Usage.md)
- After initial release, found ScreenRecording with sound was not syncing correctly - FIXED! (Home Key to toggle recording modes)

**April 19, 2026** - Fixed and add new features to the Screen Recording System:

- Added the Ability to record Microphone and volume settings using the num_keypad + and - keys for volume adjustment.
- Added an OSD when the microphone volume settings have changed and is displayed for 10 seconds after change.
- All sounds are NOW properly in sync regardless of what fps we are recording at.
- Movie Playback now supports Sound Output and can be played at said given base Frame Rate.

**April 20, 2026** - Fixed DX11 Screen Resizing Issues:

- Fixed Screen Resizing Issues.
- Fixed Render Bug in GUIManager.cpp
- Fixed compile errors for Releasse version.
- Optimisation of code.
- Clean Exit (0) {8-)

**April 21, 2026** - More updates and fixes:

- More Screen Resizing Issues found and fixed (Out to kill this mofo of a bug!) - so some very thorough testing going on over here!
- Added Pre Background (1st) Image before rendering for 3D and post 2D Images to the render pipeline - See RenderBackgroundImage().
- Music Module Restarts correctly from the beginning on Scene Switching.
- Scene Data correcly cleared up scene switching!
- Added individual import module for Blender and used on detection (BlenderImports.h/.cpp) as other systems can use GLTF/GLB as well.
- Web site has been established, demos will come very soon and please visit https://ultimanium.com/ for a check out on things you can do with this engine!  
  
  Recordings can now been done internally also with microphone, check our you-tube or our website out and find all our releases there.  
  
  Remember, you know the drill right, give us a subscribe and a like and lets get this gaming engine off the ground peeps please!  
  
  This is not just about me anymore guys and girls!
  
  It is also about you too now and what you can offer the world with your inspirations.  
  
  What I please ask, please share some of your coding ideas to us by
  contributing to our github project.  Please message the manager at my website and request for a pull request and please explain to me how
  you can contribute (You can also leave me a message on Discord in our respective CPGE Server).  
  
  All Contributions will be recorded and posted daily on my website to ensure recognision is given to those who do.  More importantly,
  thank you everybody who does as this is an FOSS project and your support is always welcomed!  How about you get onboard yeah and turn this
  to magic and have your name be recognised within the community!
  
**April 22, 2026** - More updates and fixes:

- Joystick/Gamepad reads configuration sensitivity settings and is maintained.
- Version stamping to the configuration file is now stamped and maintained.
- Music Volume Adjustments are now implemented, use ALT + NUM_KEPAD_PLUS or ALT + NUM_KEPAD_MINUS
- SFX Volume Adjustments are now implemented, use CTRL + NUM_KEPAD_PLUS or CTRL + NUM_KEPAD_MINUS
- Global Volume Adjustments are now implemented, use CTRL + SHIFT + NUM_KEPAD_PLUS or CTRL + SHIFT + NUM_KEPAD_MINUS
- TTS Volume Adjustments are now implemented, use CTRL + ALT + NUM_KEPAD_PLUS or CTRL + ALT + NUM_KEPAD_MINUS
- CMAKE Support and now can compile with it, Linux support will be added soon for compiling, but cmake can be used on linux right!?.
    Use "cmake-build.bat debug" or "cmake-build.bat release"

**April 26-27, 2026** - More updates and fixes:

- Added a loading screen during loading phase so its clear to users that data is been loaded in.
- Fixed Lighting Ordering.
- Fixed Material System (Some issues) WIP to resolve and fix.
- GLTF/GLB Model Animator will check all models on import and assign parent / child relationship.
- fxManager.CreateStarfield() now supports a default x,y,z starting position with a Reverse travel flag.
- MSBuild and Cmake will now increment all versions across all files, documents (such as this file) to ensure proper versioning!
- OSD setting changes with current OSD on screen display, will close and show new one instead!
- Added History folder, to show changes on each file to what has been exactly been changed in the 
  current updates to the repository! (Please note! Redundant information will be truncated as time passes on!)
- System for both MSBuild and CMAKE will include this file, version.id and BuildInfo.h to reflect proper
  versions in your builds (DONE FN changing this manually all the gawd damn time!!!).

`  See here now for live updates on exactly whats been changed! - But this file will always be the basis of updates
   Please see the "History" folder for actual file updates, old and redundant files will be removed! - Saves me updating
   this file all time when I do not have too; especially versioning!

### May 2026 - More major updates and fixes

**May 02, 2026** - More major updates and fixes:

- Real Time in runtime configuration management system (GUIConfigWindow.cpp) - This is your panel for 
  Game configuruation and must reflect with your Configuration.h/cpp file, bring to life now the real 
  updates of the system during runtime.
  
- Update to GFX and controls in the GUIManager.h/cpp system - added, horizontal scrollers and proper event controlling in a threaded based system.
- Moved FOV option in configuration window to Video TAB area.
- Added Loading Screen (We could animate this, but for now, Keeping It Simple Stupid (KISS).
- CMAKE now supports proper shader builds on Windows Platform.
- Various Bugs Resolved, ie.. Restart on demand, not correctly instantiating proper closer of current app, before executing a restart of app.
- No more highlighting of text in the config panel, do not what I was thinking, lot of copying and paste in the errors - 
  Just goes to show, shortcuts never pay off!

**May 03-04, 2026** - Display mode and mouse cursor system now fully config-driven:

- Startup display mode now reads `displayMode` from the configuration file (0=Windowed, 1=Borderless, 2=Full Screen) rather than being hard-coded by debug/release build type.
- **Windowed mode**: Windows cursor is visible on the title bar and resize borders so the user can drag and resize the window; hidden inside the client area where the engine renders its own cursor.
- **Borderless mode**: Window style set to WS_POPUP covering the full primary monitor; Windows cursor is hidden inside the client area and automatically visible outside the window so the user can operate the OS normally.
- **Full Screen exclusive**: Windows cursor fully suppressed via ShowCursor — the engine owns the entire display.
- Added `WM_SETCURSOR` handler to `WindowProc` to enforce client-area cursor hiding for Windowed and Borderless modes without affecting non-client area behaviour.
- Added `isBorderless` flag to `WindowMetrics` struct so runtime code can distinguish borderless from windowed.
- `StartRendererThreads()` is now called immediately after the display mode switch so the loader ring is visible throughout the entire remaining init sequence (sound, movie, joystick, callbacks) for all modes.
- **Fix — loader ring invisible after display mode change:** `SetWindowPos` calls for Windowed and Borderless modes were firing `WM_SIZE` synchronously before `StartRendererThreads()`, which triggered `Resize()` → `Clean2DTextures()` → `m_d2dRenderTarget.Reset()` and wiped all D2D texture state before the loader thread could load `BG_LOADER_CIRCLE`. Fix: flag-setting and `SetWindowLong` (borderless style) remain in the startup switch; all `SetWindowPos` geometry calls are now deferred until immediately after `StartRendererThreads()` returns, so any subsequent `WM_SIZE` executes through the designed resize path with the loader already active.

**May 06, 2026** - MoviePlayer A/V sync overhaul — audio no longer advances ahead of video:

- Fixed sync clock miscalibration: old formula compared `(videoPTS − firstVideoPTS)` against
  `SamplesPlayed / sampleRate`, which only held when both streams shared the same file start PTS.
  New formula uses absolute file PTS on both sides:
  `audioNowPTS = firstAudioSamplePTS + (totalSamples / sampleRate) × 10 000 000`.
- Fixed pause/resume audio clock reset: XAudio2 resets `SamplesPlayed` to 0 on every `Start()` 
  of a stopped voice. After a resume, the old code restarted the clock from zero, causing all 
  queued video frames to pass the sync gate simultaneously (video race, audio perceived ahead).
  `Pause()` now snapshots elapsed samples into `m_samplesPlayedOffset`; `Play()` resume resets
  `m_audioStartSamples = 0` so the clock continues unbroken across stop/start cycles.
- Activated the audio read-ahead gate: `m_audioReadPosition` was being tracked but never
  enforced. The XAudio2 feed loop now stops pre-filling if decoded audio is more than 500 ms
  ahead of the current video PTS, bounding memory usage and capping initial offset.
- *See: [`MoviePlayer.h/.cpp`](MoviePlayer.h)*

- Fixed SCENE_GAMETITLE configuration window crash on open: `GUIManager::CreateMyWindow` was
  inserting into the `windows` unordered_map without holding the GUIManager mutex. `Render()`
  iterates `windows` under that same mutex from the render thread, creating a data race that
  manifested as a heap corruption crash when the config button was clicked. Fixed by acquiring
  a `lock_guard<timed_mutex>` at the top of `CreateMyWindow`, consistent with how `RemoveWindow`
  and `Render()` already protect map access.
- *See: [`GUIManager.cpp`](GUIManager.cpp)*

- Joystick hot-plug monitoring added to the main loop — controllers can now be connected or
  disconnected at any time and the engine responds correctly:
  - `PollControllers()` re-scans all joystick slots every 2 seconds via `GetTickCount64()`
    and logs a message whenever the active controller count changes.
  - `HasActiveControllers()` inline added so joystick input and movement processing in
    `SCENE_GAMEPLAY` is skipped entirely when no controller is present (no wasted reads).
- *See: [`Joystick.h`](Joystick.h), [`Joystick.cpp`](Joystick.cpp), [`main.cpp`](main.cpp)*

- Full Vulkan renderer implementation added — all seven files written as a complete
  cross-platform port of the DirectX 11 renderer:
  - **`VULKAN_Renderer.h/.cpp`** — `VulkanRenderer : public Renderer` with Vulkan 1.2
    instance/device/swapchain/renderpass/pipeline management; Windows = D2D overlay + WIC
    textures + DirectXMath; Linux/Android = XCB/ANativeWindow + GLM; runtime GLSL→SPIR-V
    via shaderc; 2 frames-in-flight pattern; manual Vulkan memory (no VMA).
  - **`VULKAN_RenderFrame.cpp`** — Scene render loop: acquire→fence→D2D overlay→begin
    renderpass→3D scene→FX→2D overlay composite→submit→present; swap chain recreation on
    `VK_ERROR_OUT_OF_DATE_KHR`.
  - **`VULKAN_FXManager.h/.cpp`** — `VKFXManager` with Vulkan pipeline for fullscreen fade
    quad (inline GLSL, push constant color, gl_VertexIndex fullscreen triangle); all DX11
    FX ported: ColorFader, Starfield, ParticleExplosion, Scroller, TextScroller (LTOR/RTOL/
    Consistent/Movie); scroll tweens; pending callbacks; `VkCommandBuffer` in place of
    `ID3D11DeviceContext*`.
  - **`VULKAN_IOStreamThread.h/.cpp`** — `VulkanRenderer::LoaderTaskThread()` with full
    scene-switch structure; `SecureZeroMemory` guarded to Windows only; `sysUtils` and
    `CoInitialize` calls platform-gated.
  - **`Includes.h`** — Added `#if defined(__USE_VULKAN__)` block in the Windows section
    with `vulkan.h`, `vulkan_win32.h`, D2D/DWrite headers, optional shaderc, and
    `#pragma comment` link directives for `vulkan-1.lib`, `d2d1.lib`, `dwrite.lib`.
  - All 7 new files added to `CrossPlatformGameEngine.vcxproj` and `CMakeLists.txt`.
- *See: [`VULKAN_Renderer.h`](VULKAN_Renderer.h), [`VULKAN_Renderer.cpp`](VULKAN_Renderer.cpp),
  [`VULKAN_RenderFrame.cpp`](VULKAN_RenderFrame.cpp), [`VULKAN_FXManager.h`](VULKAN_FXManager.h),
  [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp), [`VULKAN_IOStreamThread.h`](VULKAN_IOStreamThread.h),
  [`VULKAN_IOStreamThread.cpp`](VULKAN_IOStreamThread.cpp), [`Includes.h`](Includes.h)*

**May 08, 2026** - 3D Warp Dot Tunnel FX system (`Init3DWarpDOTTunnel`):

- Added a new `FXType::WarpDotTunnel` effect — a Doctor Who–style 3D rotating dot-circle tunnel
  with full perspective projection, density-based ring spawning, and sine-path XY drift.
- **`Init3DWarpDOTTunnel(x, y, z, minRadius, maxRadius, TunnelSpinCycle, travelSpeed, reverseTravel, dotsPerCircle, density)`**:
  - `minRadius`/`maxRadius` — ring radius at the far end and near-camera end respectively; linearly
    interpolated over the tunnel's 800-unit Z depth.
  - `TunnelSpinCycle` — enum: `None`, `Clockwise`, `AntiClockwise`; spin speed is derived from
    `travelSpeed * 0.05` rad/s.
  - `travelSpeed` — units/second base speed; for forward travel (non-reverse) rings accelerate as
    they approach the camera (`0.5 + t * 1.5` factor); for reverse travel they decelerate (`2.0 - t * 1.5`).
  - `reverseTravel` — `false` = rings fly toward camera (Doctor Who intro); `true` = rings recede
    from camera toward focal point.
  - `density` (1–100) — number of simultaneous rings; rings are staggered evenly along the tunnel
    at startup so density is immediately uniform. Each ring resets to the far/near origin once it
    reaches its destination, with the next ring spawning at 1/density of travel elapsed.
  - Ring XY centres follow a parametric circular sine-path (radius 300 world units, one complete
    revolution per tunnel length) computed via `FAST_MATH.FastSinCos` from the precalculation
    tables — never sporadic, always a clean closed loop.
  - Dots drawn via `Blit2DColoredPixel` after perspective projection; culled on NDC bounds; size
    scales from 1 px (far) to 4 px (near); colour transitions from dim cool-blue (far) to bright
    white-blue (near); alpha-edge-fades at 8 % of travel from each end to prevent popping.
- **`StopWarpDotTunnel()`** — removes the active tunnel FX and clears `tunnelID`.
- `StopAllFXForResize()` saves `tunnelActive`/`tunnelID` and stops the tunnel before resize so
  no stale rings are left in-flight during DirectX resource recreation.
- `Render()` update loop calls `UpdateWarpDotTunnel()` alongside the existing Starfield update;
  `RenderFX()` dispatches `RenderWarpDotTunnel()` for the new FXType.
- `DXRenderFrame.cpp` and `VULKAN_RenderFrame.cpp` both call
  `fxManager.RenderFX(fxManager.tunnelID, ...)` when `tunnelID > 0`, consistent with the
  existing Starfield render call pattern.
- Full Vulkan mirror in `VKFXManager`: `VKTunnelRing`, `VKWarpTunnelData`, identical
  `Init3DWarpDOTTunnel` / `StopWarpDotTunnel` / `UpdateWarpDotTunnel` / `RenderWarpDotTunnel`.
- *See: [`DX_FXManager.h`](DX_FXManager.h), [`DX_FXManager.cpp`](DX_FXManager.cpp),
  [`VULKAN_FXManager.h`](VULKAN_FXManager.h), [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp),
  [`DXRenderFrame.cpp`](DXRenderFrame.cpp), [`VULKAN_RenderFrame.cpp`](VULKAN_RenderFrame.cpp)*

**May 10, 2026** - WarpDotTunnel fly-through camera overhaul and straight-tunnel redesign:

- **Straight tunnel geometry** — removed the sine-path XY winding from both `Init3DWarpDOTTunnel`
  and `UpdateWarpDotTunnel`. Ring centres no longer drift up to 300 world units from the tunnel
  axis; they are fixed at `(startX, startY)` throughout the tunnel. Effect is now a perfectly
  straight corridor of circles flying directly at the viewer.
- **One-time camera setup in `Init3DWarpDOTTunnel`** — camera is repositioned once when the
  tunnel starts: `SetPosition(x, y, nearZ)`, `SetTarget(x, y, farZ)`, `SetYawPitch(0, 0)`.
  This fires under the fade-to-black overlay so there is no visible snap. Camera sits at the
  tunnel entrance looking straight down the tunnel axis toward the far-end vanishing point.
- **Removed per-frame `SetPosition` from `UpdateWarpDotTunnel`** — previous iterations called
  `SetPosition()` every frame trying to track the nearest ring centre; this caused choppiness
  because it fought the camera system's internal state. Camera position is now fixed for the
  lifetime of the effect. `UpdateWarpDotTunnel` only calls `SetTarget` to keep the look-target
  locked to `(startX, startY, farZ)`.
- **Removed `pathPhaseOffset` advance** — the per-frame phase drift that drove the XY winding is
  no longer updated. The field remains in the struct but is unused.
- **`SetupDefaultCamera` now syncs `position`/`target` public members** — previously
  `SetupDefaultCamera` set the view matrix via `XMMatrixLookAtLH` but left the `position` and
  `target` public members stale. Added `position = eyePos; target = lookPos;` so that
  `GetPosition()` and `target` return the correct values immediately after a default-camera reset.
- **ESC exit — camera reset moved to after fade completes** — when ESC is pressed to return from
  the tunnel to the game menu, `SetupDefaultCamera` and `SetYawPitch` now execute after the
  `IsFadeActive()` loop (screen fully black) rather than before `FadeToBlack`. This eliminates
  the visible camera snap that was seen when exiting the effect.
- *See: [`DX_FXManager.cpp`](DX_FXManager.cpp), [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp),
  [`DXCamera.cpp`](DXCamera.cpp), [`KBHandlersCode.cpp`](KBHandlersCode.cpp)*

- **ScriptManager Class**: Full scene script execution system introduced. Scripts are plain ASCII
  `.cgs` (CPGE Game Script) files stored in `Scripts/<SCENE_NAME>.cgs` and detected automatically
  during scene initialisation. Each script carries its own `ScriptVersion` and `Written` date fields
  so individual scripts are independently versioned.
- **11 commands implemented (v1.0)**:
  - `Execute FunctionName(args)` — dispatches any of 53 registered engine API calls spanning
    FXManager, Camera, SoundManager, SceneManager, GamePlayer and GUIManager. Case-insensitive.
  - `QUIT` — clean engine shutdown via `PostQuitMessage(0)`.
  - `ALERT <General|Error|CRITICAL> "message"` — centred red/yellow alert window; CRITICAL auto-triggers QUIT.
  - `STOP <MUSIC|EFFECTS|MOUSE|GAMEPAD>` — halts the targeted subsystem; MOUSE/GAMEPAD expose
    flags (`IsMouseStopped()` / `IsGamepadStopped()`) for the main loop to honour.
  - `POSITION x y z yaw pitch` — instant camera placement and orientation.
  - `PLAY_POSITION x y z` — sets world position of all active players.
  - `GET_READY` — plays the get-ready audio cue for the current player.
  - `RESET` — stops FX/audio, clears collision rules, resets mouse/gamepad flags, returns to `SCENE_GAMETITLE`.
  - `SAVE` / `LOAD` — precache scene state to/from `Precache/scene_precache.bin`.
  - `DETECT_COLLISION typeA idxA typeB idxB [radius] <action>` — registers a per-frame collision
    rule; evaluates `PLAYER vs PLAYER` (sphere check via Physics) and `PLAYER vs WALL` (GamePlayer
    collision bitmap); fires the action string as a command on first contact (one-shot).
- **Execution modes**: synchronous (`ExecuteScript`) or detached background thread (`ExecuteScriptAsync`);
  stop requested via atomic flag at each command boundary.
- **Six scene scripts created** in new `Scripts/` directory:
  `SCENE_INITIALISE`, `SCENE_GAMETITLE`, `SCENE_GAMEPLAY`, `SCENE_INTRO`, `SCENE_GAMEOVER`, `SCENE_CREDITS`.
- **`WAIT(seconds)` command added**: Pauses script execution for the specified duration before
  proceeding to the next command. Interruptible via `StopExecution()` — checked every 50 ms so
  shutdown is never blocked. Supports both `WAIT(2.5)` and `WAIT 2.5` syntax forms.
- **`LABEL <Name>:` and `GOTO <Name>` commands added**: Full label/jump system within a script.
  Labels are resolved into a name→index map at load time so forward and backward references both
  work. `GOTO` redirects the execution loop to the command after the named label. Labels are
  case-insensitive; the trailing colon is optional. A 1,000,000-step guard prevents runaway
  infinite loops from locking the engine.
- *See: [`ScriptManager.h`](ScriptManager.h), [`ScriptManager.cpp`](ScriptManager.cpp),
  [`Scripts/`](Scripts/), [`Docs/Scripting-Example-Usage.md`](Docs/Scripting-Example-Usage.md)*

**May 11, 2026** - Bug fixes, hardcoded resolution audit, and ScriptManager pipeline integration:

- **Bug fix — full-screen quarter-screen rendering**: `DX11Renderer::SetupViewport()` had hardcoded
  `DEFAULT_WINDOW_WIDTH` (800) and `DEFAULT_WINDOW_HEIGHT` (600). On a 1600×1200 display those values
  are exactly 1/4 of the screen area (half width × half height), producing the reported quarter-screen
  symptom in fullscreen mode.
  - `SetupViewport()` now uses `m_renderTargetWidth` / `m_renderTargetHeight`, populated from the
    actual swap chain back buffer by `CreateRenderTargetViews()`.
  - `Initialize()` now seeds `m_renderTargetWidth/Height` from `config.myConfig.resolutionWidth/Height`
    at startup before any D3D resource creation.
  - All renderer headers (`DX11Renderer.h`, `DX12Renderer.h`, `OpenGLRenderer.h`, `VULKAN_Renderer.h`)
    member initializers changed from `DEFAULT_WINDOW_WIDTH/HEIGHT` to `0`.
  - *See: [`DX11Renderer.cpp`](DX11Renderer.cpp), [`DX11Renderer.h`](DX11Renderer.h)*

- **Bug fix — mouse cursor confined to 800×600 in fullscreen exclusive mode**:
  `SetFullExclusive()` updated `iOrigWidth/iOrigHeight` to the real monitor resolution but never
  synced `winMetrics.width/height`. The `WM_MOUSEMOVE` cursor clamp reads `winMetrics.width/height`,
  so any cursor position beyond the original HWND size (800×600) was silently clamped to 799×599.
  - `SetFullExclusive()` now updates `winMetrics.width/height/clientWidth/clientHeight`,
    `m_renderTargetWidth/Height`, and calls `myCamera.UpdateResolution()` with the actual monitor
    resolution and canonical aspect ratio immediately after the buffers are resized.
  - *See: [`DX11Renderer.cpp`](DX11Renderer.cpp)*

- **Bug fix — camera constructor hardcoded 800×600**: `Camera::Camera()` initialised
  `m_screenWidth/Height` from `fDEFAULT_WINDOW_WIDTH/HEIGHT` and called
  `SetupDefaultCamera(800, 600)` regardless of the configured resolution. Camera projection was
  therefore wrong until the first `UpdateResolution()` call arrived via `WM_SIZE`.
  - Constructor now reads `config.myConfig.resolutionWidth/Height` and calls
    `LookupAspectRatio()` — consistent with every other subsystem that tracks screen dimensions.
  - *See: [`DXCamera.cpp`](DXCamera.cpp)*

- **Hardcoded resolution audit — remaining instances eliminated**:
  - `Configuration.h` — `MyConfig` struct defaults changed from `resolutionWidth=1920`,
    `resolutionHeight=1080`, `displayMode=2` (Full Screen) to `resolutionWidth=800`,
    `resolutionHeight=600`, `displayMode=0` (Windowed). If `GameConfig.cfg` is absent or corrupt,
    `loadConfig()` returns false and the struct defaults take effect — engine now falls back safely
    to 800×600 windowed instead of attempting 1920×1080 fullscreen.
  - `DX_FXManager.h` — `TextScrollData` default region changed from raw literals `800.0f/600.0f`
    to `fDEFAULT_WINDOW_WIDTH/fDEFAULT_WINDOW_HEIGHT` (numerically identical but now self-documenting).
  - *See: [`Configuration.h`](Configuration.h), [`DX_FXManager.h`](DX_FXManager.h)*

- **ScriptManager integrated into the render and scene pipeline**: The `__USE_SCRIPT_MANAGER__`
  conditional compilation guard is now fully wired throughout `main.cpp` and `DXRenderFrame.cpp`.
  All call sites are wrapped in `#ifdef __USE_SCRIPT_MANAGER__` so the system compiles away to
  nothing when the define is commented out in `Includes.h`.
  - `main.cpp`: added `#include "ScriptManager.h"` guard; `ScriptManager scriptManager;` global
    instance; `scriptManager.Initialize(...)` + `LoadSceneScript(SCENE_INITIALISE)` +
    `ExecuteScriptAsync()` called after `scene.Initialize(renderer)` at startup.
  - Scene transition functions — `SwitchToGamePlay()`, `SwitchToMovieIntro()`,
    `SwitchToGameIntro()` — each call `StopExecution()` before the scene switch and
    `LoadSceneScript()` + `ExecuteScriptAsync()` immediately after `InitiateScene()`.
  - `DXRenderFrame.cpp`: `extern ScriptManager scriptManager` declared under the guard;
    `scriptManager.Update(deltaTime)` called each frame after the delta-time clamp so
    `DETECT_COLLISION` rules are evaluated every rendered frame.
  - *See: [`main.cpp`](main.cpp), [`DXRenderFrame.cpp`](DXRenderFrame.cpp),
    [`ScriptManager.h`](ScriptManager.h), [`ScriptManager.cpp`](ScriptManager.cpp)*

**May 14, 2026** - ScriptManager v1.1 — Variables and FOR loop support added:

- **VAR directive** — typed global variable declarations added to the script language.
  Four types supported: `int` (C++ `int`), `bool` (C++ `bool`), `float` (C++ `float`),
  `string` (C++ `std::wstring`). Syntax: `VAR <type> <Name> = <value>;` (trailing
  semicolon optional). Variables must be declared at the top of the script body, before
  any executable commands — placement after a command is detected at parse time and
  logged as an error.
  - Runtime: `Cmd_VarDecl()` populates `m_variables` (a `std::unordered_map<std::string, ScriptVar>`)
    on each script execution, so variables are always re-initialised to their declared values.
  - `ScriptVar` struct stores typed value fields (`intVal`, `boolVal`, `floatVal`, `strVal`)
    alongside a `Type` enum; helper functions `GetVarAsFloat()` / `SetVarFromFloat()` read
    and write variables uniformly regardless of declared type.

- **FOR / BEGIN / END loop** — counted loop construct added to the script language.
  Full syntax: `FOR <Var> = <start> TO <end> [STEP <n>] DO / BEGIN … END`.
  - Direction (forward or reverse) is inferred automatically: `start < end` increments,
    `start > end` decrements; the `STEP` value is always positive.
  - Forward exit condition: counter `>=` end (checked after each increment at `END`).
  - Reverse exit condition: counter `<=` end (checked after each decrement at `END`).
  - If start already satisfies the exit condition, the loop body is skipped entirely (zero iterations).
  - Nested loops are fully supported via a runtime `m_loopStack` (`std::vector<LoopFrame>`);
    each frame stores variable name, end value, step, direction flag, body-start index, and
    END_BLOCK index.
  - `BuildLoopMap()` runs once after parsing: a single-pass stack scan cross-links every
    `FOR_LOOP` command to its matching `END_BLOCK` via a new `blockPeer` field on
    `ScriptCommand`, resolving all loop jumps to O(1) index lookups at runtime.
  - `BEGIN` is a structural no-op; `Cmd_ForLoop()` auto-detects and skips it when computing
    the body-start index so `BEGIN` is optional but recommended for readability.
  - The existing 1,000,000-step guard still protects against runaway loops.

- **ScriptManager added to build system** — `ScriptManager.cpp` and `ScriptManager.h` were
  tracked by git but absent from both `CMakeLists.txt` and `CrossPlatformGameEngine.vcxproj`.
  The file compiled to nothing and no changes were ever validated by the build. Both build
  files updated; first confirmed clean compile as part of this session (v0.0.1149 Debug).

- **`Docs/Scripting-Example-Usage.md` updated to v1.1** — document version, Table of Contents,
  Script File Format, Versioned Header, Command Reference, and Error Handling table all
  updated to cover `VAR`, `FOR`, `BEGIN`, `END` with full syntax tables, termination rules,
  and worked examples for all four loop forms (forward step 1, forward step 2, reverse step 1,
  reverse step 2) including a nested loop example.

- *See: [`ScriptManager.h`](ScriptManager.h), [`ScriptManager.cpp`](ScriptManager.cpp),
  [`CMakeLists.txt`](CMakeLists.txt), [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj),
  [`Docs/Scripting-Example-Usage.md`](Docs/Scripting-Example-Usage.md)*

**May 14, 2026** - SCENE_EXPERIMENT black screen fix — WarpDotTunnel not rendering:

- **Bug fix — SCENE_EXPERIMENT rendered a pure black screen with no tunnel visible**: `RenderBackgroundImage()`
  had no case for `SCENE_EXPERIMENT` in its scene switch, so it fell through to `default: break` every frame.
  The `fxManager.RenderFX(fxManager.tunnelID, ...)` call that drives the WarpDotTunnel was only wired inside
  the `SCENE_GAMETITLE` case — never reached when the experiment scene was active.
  - Added a `#if defined(_DEBUG)` guarded `SCENE_EXPERIMENT` case to `RenderBackgroundImage()` that calls
    `fxManager.RenderFX(fxManager.tunnelID, m_d3dContext.Get(), myCamera.GetViewMatrix())` once
    `bLoaderTaskFinished` is true. Background stays black (D3D clear); tunnel dots render on top.
    Camera target is set internally by `UpdateWarpDotTunnel` — no extra setup required.
- *See: [`DXRenderFrame.cpp`](DXRenderFrame.cpp)*

- **Bug fix — WarpDotTunnel initialisation was happening on the main thread in GUIWindows.cpp**
  instead of in the loader thread (`IOStreamDX11Thread.cpp`) where all scene setup belongs.
  `SaveAndSuspendFXForScene()` and `Init3DWarpDOTTunnel()` were being called in the
  experimental button handler before `ResumeLoader()` was even called — violating the
  established pattern where the loader thread owns all scene resource and effect setup.
  - Removed `SaveAndSuspendFXForScene()` and `Init3DWarpDOTTunnel()` from the
    `experimentalButton.onMouseBtnDown` handler in `GUIWindows.cpp`. The button handler
    now only fades to black, removes the menu window, switches scene, and calls
    `ResumeLoader()` — consistent with how all other scene buttons work.
  - Added `SaveAndSuspendFXForScene()` and `Init3DWarpDOTTunnel()` to the
    `SCENE_EXPERIMENT` case in `IOStreamDX11Thread.cpp`, after textures are loaded and
    before `bLoaderTaskFinished` is set and `FadeToImage` is called. Screen is already
    black when the loader thread runs, so the tunnel is initialised and rendering before
    the fade-in begins.
- *See: [`GUIWindows.cpp`](GUIWindows.cpp), [`IOStreamDX11Thread.cpp`](IOStreamDX11Thread.cpp)*

- `cmake-build.bat clean` now performs a full wipe of all compiled artifacts in one step:
  - `build\` — CMake build directory (Debug and Release subdirectories)
  - `x64\` — Visual Studio output directory (PDB, EXE)
  - `CrossPla.2bd3f178\` — Visual Studio intermediate directory (OBJ, IDB, ILK)
  - Root-level `*.pdb`, `*.ilk`, `*.obj`, `*.pch`, `*.idb` files
  - Match is case-insensitive (`CLEAN`, `Clean`, `clean` all work).
- *See: [`cmake-build.bat`](cmake-build.bat)*

- Both call sites in `IOStreamDX11Thread.cpp` (initial scene load and post-resize) previously passed
  `iOrigWidth`/`iOrigHeight` directly to `SetupDefaultCamera`, with no regard for whether the engine
  was running in Windowed, Borderless, or Fullscreen exclusive mode.
- Replaced both calls with a `switch` on `config.myConfig.displayMode`:
  - **Fullscreen (2)** — uses `config.myConfig.resolutionWidth` / `resolutionHeight` (the configured
    target resolution).
  - **Borderless (1)** — uses `winMetrics.monitorFullArea` rect dimensions (full physical monitor area).
  - **Windowed (0)** — uses `winMetrics.clientWidth` / `clientHeight` (drawable client area only,
    excluding title bar and borders).
- `winMetrics` is always refreshed by `GetWindowMetrics()` in `main.cpp` after every resize, so both
  paths read current live values regardless of when the loader thread runs.
- *See: [`IOStreamDX11Thread.cpp`](IOStreamDX11Thread.cpp)*

**May 14, 2026** - Renderer selection added to configuration system:

- `config.myConfig.rendererType` (int) added to `MyConfig` — persisted to `GameConfig.cfg` as
  `"rendererType"`. Not included in the checksum (consistent with all other display settings).
  Platform-valid defaults applied at load time and on every save via
  `Configuration::ValidateRendererForPlatform()`.
  - **Windows:** 0=DirectX 11 (default), 1=DirectX 12, 2=OpenGL, 3=Vulkan
  - **Linux/Android:** 0=OpenGL (default), 1=Vulkan
  - **iOS/macOS:** 0=OpenGL (only option — value is always clamped to 0)
- `Includes.h` updated: all platform-valid renderer defines are now enabled simultaneously
  rather than one at a time. Windows compiles all four backends; Linux/Android compiles
  OpenGL and Vulkan; iOS/macOS compiles OpenGL. This is required so `RendererFactory.cpp`
  can instantiate any backend at runtime without a recompile.
- `RendererFactory.cpp` rewritten to read `config.myConfig.rendererType` at runtime. A
  `switch` dispatches to the correct `std::make_shared<>` call, guarded per-case by
  `#if defined(__USE_*)` so a missing backend falls to `default: EXIT_FAILURE` rather than
  silently selecting the wrong one. Logs the chosen renderer type on success.
- **Video tab — Renderer slider added**: appears below Display Mode in the config window.
  Shows only the platform-valid options (hidden entirely on iOS/macOS where OpenGL is
  the only choice). Changing the value sets `needsVideoRestart = true`, so the existing
  10-second restart notification fires on Save.
- *See: [`Configuration.h`](Configuration.h), [`Configuration.cpp`](Configuration.cpp),
  [`Includes.h`](Includes.h), [`RendererFactory.cpp`](RendererFactory.cpp),
  [`GUIConfigWindow.cpp`](GUIConfigWindow.cpp), [`Renderer.h`](Renderer.h)*

**May 15, 2026** - WarpDotTunnel speed profile overhaul:

- **WarpDotTunnel — forward direction quadratic acceleration**: Ring travel speed now uses a
  quadratic curve instead of a linear ramp. Rings are very slow far from the camera and
  accelerate dramatically as they approach, giving a much more visceral warp-tunnel feel.
  - Speed formula changed from `0.5 + pathT × 1.5` to `0.1 + pathT² × 2.4`
  - At far end (pathT = 0): speed factor ≈ **0.1×** (barely moving)
  - At near end (pathT = 1): speed factor = **2.5×** (very fast)
  - *See: [`DX_FXManager.cpp`](DX_FXManager.cpp), [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp)*

- **WarpDotTunnel — reverse direction speed profile corrected**: The reverse (`reverse=true`)
  speed profile was inverted — rings were slow at the camera end and fast at the vanishing point,
  which is the opposite of the intended feel. Formula fixed to be very fast at the near end and
  decelerate smoothly toward the vanishing point.
  - Old (wrong): `2.0 − pathT × 1.5` → slow at start (0.5×), fast at end (2.0×)
  - New (correct): `0.2 + pathT × 2.3` → very fast at start (2.5×), slow at end (0.2×)
  - *See: [`DX_FXManager.cpp`](DX_FXManager.cpp), [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp)*

- Microphone volume maximum raised from 10 to 20 across the entire system:
  - `ScreenRecorder.h` — `SetMicMonitorGain` and `SetMicRecordGain` clamp raised from 4.0 → 20.0.
  - `GUIConfigWindow.cpp` — Microphone Volume slider range and clamp updated from `0–10` to `0–20`.
  - `main.cpp` — NUMPAD +/- keyboard OSD clamp raised from `10.0` to `20.0`.
  - OSD label thresholds updated: "(boosted)" now triggers at ≥1000% (gain > 10.0), "(max)" at ≥1900% (gain ~19+).
- **Bug fix — record gain never updated from config**: `m_micRecordGain` was initialised to 2.5
  and never changed — only `m_micMonitorGain` was set from `microphoneVolume`. The mic appeared
  soft in recordings regardless of the volume setting because the blend gain was always fixed at 2.5.
  - Startup init, `applyLive` callback, and NUMPAD keyboard handler all now call `SetMicRecordGain`
    alongside `SetMicMonitorGain`, keeping monitor and record gain permanently in sync.
- *See: [`ScreenRecorder.h`](ScreenRecorder.h), [`GUIConfigWindow.cpp`](GUIConfigWindow.cpp), [`main.cpp`](main.cpp)*

**May 16, 2026** - Console Output Window (F8 toggle):

- **ConsoleWindow class** introduced — a lightweight, scene-aware OSD console rendered directly
  via the `Renderer` interface (no GUIManager dependency).
- **50-line circular buffer** (`std::deque<ConsoleLine>`): oldest line evicted automatically when
  the buffer exceeds the limit. Thread-safe for `AddLine()` called from any thread.
- **14-line visible area** with newest line always at the bottom and empty slots pushed to the top
  when the buffer is sparse.
- **Vertical scrollbar** on the far-right edge of the window: thumb is proportional to the
  filled fraction of the buffer and positioned at the bottom when showing the newest lines.
  Scrollable with **Page Up / Page Down** (3 lines per keypress); only active when console is visible.
- **"Console" titlebar** across the top of the window.
- **Small font** — size is computed as `clamp(screenHeight / 108, 8, 12)` giving ~10pt at 1080p,
  scaling down to 8pt on smaller displays and capped at 12pt on large ones.
- **Scene restriction** — only rendered in `SCENE_GAMETITLE` and `SCENE_GAMEPLAY`. The render site
  in `DXRenderFrame.cpp` skips the draw while `scene.bSceneSwitching` is true, hiding the console
  automatically during all scene transitions without changing `bIsVisible`.
- **F8 key handler** added in `KBHandlersCode.cpp`: toggles `consoleWindow.Toggle()` only when
  the current scene is `SCENE_GAMETITLE` or `SCENE_GAMEPLAY`; ignored in all other scenes.
- **Window position** — bottom margin raised to 50 px so the console clears the Windows taskbar.
- **Window size** — width clamp raised to 80 % of screen width / max 1400 px; visible
  lines raised from 10 to 14 for a taller content area.
- **Text no-wrap** — lines are drawn with the unbounded `DrawMyText` overload (no size rect) so
  long lines extend to the right rather than wrapping to a second row.
- **Mouse input** — scrollbar now responds to the mouse in addition to Page Up / Page Down:
  - **Mouse wheel** — when the console is visible the wheel is fully captured and scrolls
    the buffer 3 lines per tick; camera zoom is suppressed while the console is open.
  - **Scrollbar click** — clicking anywhere on the scrollbar track jumps directly to that
    position in the buffer (top = oldest lines, bottom = newest).
  - Scrollbar geometry (x, y, height) is cached each `Render()` frame for zero-cost hit-testing.
- **Buffer raised** — `CONSOLE_MAX_LINES` increased from 50 to 100 lines.
- **Debug system integration** — in `_DEBUG` builds all `Debug::` output paths forward to the
  console window with per-line colour:
  - `LOG_WARNING` / `LogWarning()` → **yellow** `(255, 220, 0)`
  - `LOG_ERROR` / `LOG_CRITICAL` / `LogError()` → **orange** `(255, 140, 0)`
  - All other levels / `Log()` / `DebugLog()` / `LogFunction()` → **white** `(210, 210, 210)`
  - Forwarding is inside `#ifdef _DEBUG` blocks; Release builds unchanged.
- *See: [`ConsoleWindow.h`](ConsoleWindow.h), [`ConsoleWindow.cpp`](ConsoleWindow.cpp),
  [`Debug.cpp`](Debug.cpp), [`KBHandlersCode.cpp`](KBHandlersCode.cpp),
  [`DXRenderFrame.cpp`](DXRenderFrame.cpp)*

**May 16, 2026** - WarpDotTunnel visual and motion enhancements:

- **Sequential gray color cycling**: Each ring is now assigned a fixed shade from an 8-step
  dark-to-white gray ramp (`colorStep = ringIndex % 8`). Shades cycle sequentially across
  rings so the tunnel always shows the full gradient simultaneously — very dark grey, through
  mid-tones, up to pure white — and the pattern wraps cleanly when ring count exceeds 8.
  - Ramp values: `{0.08, 0.19, 0.30, 0.44, 0.58, 0.72, 0.86, 1.0}` (linear-ish perceptual steps)
  - Replaces the previous blue-tinted brightness that was tied to per-ring `pathT` depth.
  - `kGraySteps = 8` constant added to `WarpTunnelData` / `VKWarpTunnelData`.
  - `colorStep` field added to `TunnelRing` / `VKTunnelRing`.

- **Quartic forward acceleration with 1.0× minimum**: Speed floor raised so far rings are
  always visibly moving; quartic surge retained for the explosive close approach.
  Formula: `1.0 + t⁴ × 10.0` (forward), `1.0 + t⁴ × 6.0` (reverse).
  - Far end (t = 0): factor = **1.0×** → 80 units/s (clearly moving at all times)
  - Camera (t = 1): factor = **11.0×** → 880 units/s (violent final rush)
  - Average ~3.0× → 240 units/s → ~3.3-second full ring pass at `travelSpeed=80`.

- **Straight-line ring travel with per-ring birth offset (`bornCx`/`bornCy`)**: Rings no
  longer drift laterally during flight. Each ring is assigned a birth position at spawn/reset
  from a **circular** (sin X, cos Y) wave, then travels dead-straight from there to camera.
  - At wrap: `phase = sideWaveTime × kSideWaveSpeed`; `bornCx = startX + kSideWaveRadius × sin(phase)`;
    `bornCy = startY + kSideWaveRadius × cos(phase)`. X and Y are 90° out of phase so the far end
    traces a circle, not a diagonal line — gives the proper Doctor Who helical corridor feel.
  - At init: rings staggered with `sin/cos(fraction × 2π)` — circular distribution from frame 1.
  - `ring.cx = ring.bornCx`, `ring.cy = ring.bornCy` throughout — immutable after birth.
  - `bornCx` and `bornCy` fields added to `TunnelRing` / `VKTunnelRing`.
  - `kSideWaveRadius` and `kSideWaveSpeed` constants added to both structs:
    DX: `kSideWaveRadius = 80.0`, `kSideWaveSpeed = 0.85 rad/s` (wider, faster sway).
    VK: `kSideWaveRadius = 60.0`, `kSideWaveSpeed = 0.50 rad/s` (narrower, slower sway).

- **Camera look-ahead tuned to 20 rings**: Look-ahead index raised to `min(19,…)`. Camera
  now tracks the **20th-nearest ring**, providing a much more distant aim point that smooths
  out the visual jerkiness caused by individual fast-moving near rings.

- **Smooth camera transitions — exponential ease on look target**: The camera no longer
  snaps to the computed look-ahead ring position each frame. A `smoothLookTarget` is stored
  in `WarpTunnelData` and updated per-frame using a framerate-independent exponential lerp:
  `alpha = 1 − exp(−kCameraSmooth × dt)`.
  - At 60 fps the camera closes the gap smoothly each frame — no snap.
  - `smoothLookTarget` is seeded to `(startX, startY, farZ)` at init so the first frame
    aims straight down the tunnel with no startup snap.
  - `kCameraSmooth` is a `static constexpr` in both structs for easy tuning:
    DX: `kCameraSmooth = 4.0` (~95 % gap closed in ~0.75 s — snappier).
    VK: `kCameraSmooth = 3.0` (~95 % gap closed in ~1 s — slightly lazier).

- *See: [`DX_FXManager.h`](DX_FXManager.h), [`DX_FXManager.cpp`](DX_FXManager.cpp),
  [`VULKAN_FXManager.h`](VULKAN_FXManager.h), [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp)*

**May 16, 2026** - WarpDotTunnel reverse-mode spin direction fix:

- **Bug fix — reverse travel spin direction inverted**: When `reverseTravel = true`, rings
  travel away from the camera. Because the direction of travel is opposite to forward mode,
  the perceived rotation direction was also reversed — specifying `TunnelSpinCycle::Clockwise`
  in reverse mode caused the rings to appear counter-clockwise, and vice versa.
  - Fix: the spin delta is negated when `reverseTravel = true`:
    `spinDelta = (reverseTravel ? -spinSpeed : spinSpeed) * dt`
  - Result: `Clockwise` and `AntiClockwise` now produce consistent visual results regardless
    of travel direction. The fix is applied identically in both `DX_FXManager.cpp` and
    `VULKAN_FXManager.cpp`.
- *See: [`DX_FXManager.cpp`](DX_FXManager.cpp), [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp)*

**May 17, 2026** - GUIManager Z-order input routing — topmost window only receives mouse input:

- **`GUIWindow::zOrder` field added**: integer assigned at `CreateMyWindow` from a monotonic
  `m_nextZOrder` counter on `GUIManager`. Windows created later get higher Z-values and are
  therefore considered to be on top. No external callers need to manage this; it is set
  automatically.
- **`Render()` Z-order sort**: the render snapshot is now sorted by `zOrder` ascending before
  drawing, so the lowest window is drawn first and the highest is drawn last (visually on top).
  Previously the render order was determined by `unordered_map` iteration, which is undefined.
- **`HandleAllInput()` Z-order routing**: `validWindows` is sorted by `zOrder` descending
  (topmost first). Input is routed by bounding-box test in that order:
  - The first (topmost) window whose bounds contain the mouse position receives both
    `HandleMouseClick` and `HandleMouseMove`; all lower windows are skipped for new input.
  - Lower windows have their `isHovered` flags cleared so controls do not stay highlighted
    when occluded.
  - An in-progress drag (`isDragging`) or pressed slider (`isPressed`) in a lower window
    continues to receive `HandleMouseMove` so the interaction is not frozen mid-drag when
    the cursor leaves the window bounds. On mouse release those states are cancelled.
- *See: [`GUIManager.h`](GUIManager.h), [`GUIManager.cpp`](GUIManager.cpp)*

**May 17, 2026** - Renderer selector always visible in Video tab; `rendererType` added to checksum:

- **Renderer slider now always shown in Video tab**: Previously the `t2_renderer` HSlider was
  compiled away entirely when only DirectX 11 was present (`#if RENDERER_MAX > 0` guard).
  The guard has been removed — the slider is always emitted. When only DX11 is built
  (`RENDERER_MAX = 0`), the slider min and max are both 0 so the thumb is non-draggable and
  the readout shows "DirectX 11" as a fixed label. On builds with multiple backends the slider
  offers the full platform-valid range as before.
- **Renderer slider moved directly under Field of View**: The row order in the Video tab is now
  FOV → Renderer → Screen Display → Resolution → Refresh Rate → toggles. Previously Renderer
  appeared after Screen Display.
- **`rendererType` added to checksum**: `Configuration::calculateChecksum()` now includes
  `cfg.rendererType` in the FNV-1a hash string. Previously the field was persisted to
  `GameConfig.cfg` but excluded from tamper-proof validation; changing `rendererType` directly
  in the JSON file would pass the checksum check silently. Existing saved configs will fail the
  new checksum and reset to defaults on first load — the engine will immediately write a fresh
  config with the correct checksum.
- *See: [`GUIConfigWindow.cpp`](GUIConfigWindow.cpp), [`Configuration.cpp`](Configuration.cpp)*

**May 17, 2026** - OpenGL and Vulkan now visible in Video tab renderer selector:

- **Bug fix — OpenGL and Vulkan missing from renderer list**: `__USE_OPENGL__` and `__USE_VULKAN__`
  were commented out in `Includes.h`, so `RENDERER_MAX` evaluated to 0 at compile time and the
  slider was locked to DirectX 11 only. Uncommented both defines for the Windows platform block;
  the Video tab now correctly shows DirectX 11, OpenGL, and Vulkan on NVIDIA hardware.
- **Secondary fix — renderer slider now uses a compile-time list**: Replaced `RENDERER_NAMES[]` +
  `RENDERER_MAX` with an `AVAILABLE_RENDERERS[]` list containing only backends whose `__USE_*`
  define is active. Slider position maps to list index so DX12 (define still commented out) is
  never shown even with Vulkan enabled. Slider hidden entirely when only one backend is compiled.
- *See: [`Includes.h`](Includes.h), [`GUIConfigWindow.cpp`](GUIConfigWindow.cpp)*

- **Build fix — wrong OpenGL headers and include order in `Includes.h`**: Two rounds of
  fixes were needed. First round: `#include "opengl32.h"` (the lib name, not a header)
  and `#include "GL\glew32.h"` (wrong path) caused mass C1083 in every TU. Second round:
  GLEW was found at `include\GL\glew.h` but `<GL/gl.h>` was included before it — GLEW
  requires being included FIRST because it replaces gl.h's declarations (`#error: gl.h
  included before glew.h` in every TU). Final correct order:
  - `#if __has_include(<GL/glew.h>)` → include GLEW first (it pulls in gl.h internally),
    then link `glew32.lib`; else fall back to bare `<GL/gl.h>` from the Windows SDK
  - `<GL/glu.h>` included after (Windows SDK GLU utilities)
  - `glu32.lib` added; `glfw3.lib` removed (not installed)
- *See: [`Includes.h`](Includes.h)*

**May 17, 2026** - Vulkan SDK auto-detection added to CMake and VS2022 build systems:

- **`CMakeLists.txt`** — added a `find_package(Vulkan)` detection block that:
  - First attempts CMake's built-in `FindVulkan` module (reads the `VULKAN_SDK` env var set
    by the LunarG SDK installer and also checks `VK_SDK_PATH` / system PATH).
  - Falls back to a drive-root scan (`C:..G:\VulkanSDK\*`, `%ProgramFiles%\VulkanSDK\*`)
    if the env var is absent, setting `ENV{VULKAN_SDK}` and retrying `find_package`.
  - Emits a `FATAL_ERROR` with full installation instructions if the SDK is still not
    found after the scan.
  - Adds `${Vulkan_INCLUDE_DIRS}` to `target_include_directories` and the SDK `Lib`
    directory to `target_link_directories` so `#pragma comment(lib, "vulkan-1.lib")`
    resolves at link time.
- **`CrossPlatformGameEngine.vcxproj`** — added `$(VULKAN_SDK)\Include` to
  `AdditionalIncludeDirectories` and `$(VULKAN_SDK)\Lib` to `AdditionalLibraryDirectories`
  in both Debug and Release `ItemDefinitionGroup` blocks. MSBuild reads `$(VULKAN_SDK)`
  directly from the system environment variable set by the LunarG installer.
- *See: [`CMakeLists.txt`](CMakeLists.txt), [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj)*

**May 17, 2026** - Build error fixes following Vulkan SDK integration and multi-renderer enablement:

- **`VkAspectFlags` → `VkImageAspectFlags`**: `VkAspectFlags` is not a Vulkan SDK type. Corrected
  the declaration in `VULKAN_Renderer.h` and the definition in `VULKAN_Renderer.cpp` to use
  `VkImageAspectFlags`. This also resolved the secondary `CreateImageView` "does not take 3
  arguments" errors which were a cascading parse failure from the unknown type.
- **`RENDERER_NAME` redefinition**: `DX11Renderer.h` and `OpenGLRenderer.h` both defined a global
  `const std::string RENDERER_NAME`. When all renderers are enabled simultaneously, both headers
  are included in the same translation unit, causing a C2374 conflict. Added `#ifndef
  RENDERER_NAME_DEFINED` / `#define RENDERER_NAME_DEFINED` guards to both headers so only the
  first-included definition is compiled.
- **`OpenGLRenderer::Resize` return type**: Base class `Renderer::Resize` is declared `virtual bool`
  but `OpenGLRenderer::Resize` was declared `void`, causing a C2555 covariant-return error. Fixed
  the declaration in `OpenGLRenderer.h` and added the missing implementation in `OpenGLRenderer.cpp`
  (updates viewport via `glViewport`, returns `true` on success).
- **`Model` missing `IsActive` / `HasGeometry` / `GetWorldMatrix`**: `VULKAN_RenderFrame.cpp`
  called these methods on `Model` but they did not exist. Added all three as inline methods to
  the `Model` class in `Models.h`. `DrawVulkan` was also called but Vulkan GPU buffers (`VkBuffer`,
  `VkDescriptorSet`) are not yet provisioned in `ModelInfo`; the call is replaced with a `TODO`
  comment until the Vulkan model infrastructure is wired in.
- **`ThreadManager::StartThread` called with 2 arguments**: The correct pattern is
  `SetThread(id, lambda)` followed by `StartThread(id)` (void return). `VulkanRenderer::StartRendererThreads`
  was passing the lambda directly to `StartThread`, which only accepts the thread ID. Fixed to
  use `SetThread` + `StartThread` for both `THREAD_RENDERER` and `THREAD_LOADER`, with success
  reported via `DoesThreadExist`.
- **GLEW `GLEWAPI` C4005 warning**: `wglew.h` (included internally by `glew.h`) redefines
  `GLEWAPI`. Suppressed with `#pragma warning(push/disable: 4005/pop)` around the `glew.h`
  include in `Includes.h`.
- *See: [`VULKAN_Renderer.h`](VULKAN_Renderer.h), [`VULKAN_Renderer.cpp`](VULKAN_Renderer.cpp),
  [`DX11Renderer.h`](DX11Renderer.h), [`OpenGLRenderer.h`](OpenGLRenderer.h),
  [`OpenGLRenderer.cpp`](OpenGLRenderer.cpp), [`Models.h`](Models.h),
  [`VULKAN_RenderFrame.cpp`](VULKAN_RenderFrame.cpp), [`Includes.h`](Includes.h)*

**May 17, 2026** - Build error fixes round 2 — old project header contamination and GLEW:

- **`RendererFactory.cpp` included `"VulkanRenderer.h"` (old filename)**: The new file is named
  `VULKAN_Renderer.h`. Because `F:\Projects\C++\CPGE` is on the include path and the old project
  has a file named `VulkanRenderer.h`, the compiler resolved it to the OLD project's header
  (`F:\Projects\C++\CPGE\VulkanRenderer.h`). That old header chains into `F:\Projects\C++\CPGE\Includes.h`,
  `Renderer.h`, `Vectors.h`, `Color.h`, `Debug.h`, `DXCamera.h`, etc., causing every type and
  constant to be defined a second time in the same translation unit — all 40+ C2374/C2011
  redefinition errors, the `Renderer` base class confusion, and the `VulkanRenderer →
  shared_ptr<Renderer>` assignment failure. Fixed: `#include "VulkanRenderer.h"` →
  `#include "VULKAN_Renderer.h"`. Also resolved the GLEW C4005 warning that appeared in
  `RendererFactory.cpp` via the same old-header chain.
- **GLEW `GLEWAPI` C4005 in `OpenGLRenderer.h`**: The local GLEW copy does not internally chain-
  include `wglew.h` from `glew.h`. `OpenGLRenderer.h` included both `<GL/glew.h>` and
  `<GL/wglew.h>` explicitly. Because `Includes.h` already included `glew.h` with a pragma
  suppress, the `glew.h` include in `OpenGLRenderer.h` was a no-op (pragma-once), but the
  explicit `wglew.h` was processed fresh, redefining `GLEWAPI`. Fixed by wrapping both lines
  in `OpenGLRenderer.h` with `#pragma warning(push/disable: 4005/pop)`.
- *See: [`RendererFactory.cpp`](RendererFactory.cpp), [`OpenGLRenderer.h`](OpenGLRenderer.h)*

**May 17, 2026** - Full OpenGL renderer implementation — all source files complete:

- **`OpenGLFXManager.h/.cpp`** — complete `GLFXManager` class, the OpenGL-native equivalent of
  `DX_FXManager`/`VKFXManager`. All effect types ported: `ColorFader` (fullscreen-triangle GLSL
  fade quad, push-color uniform), `Starfield` (3D→2D perspective projection via z-depth division),
  `WarpDotTunnel` (dot projection, quartic acceleration, sequential gray cycling, circular birth
  offsets), `ParticleExplosion`, `Scroller` (parallax layers with scroll tweens), and all four
  `TextScroller` variants (LTOR, RTOL, Consistent, Movie) using GDI-to-texture text rendering.
  OpenGL is a state machine — no command buffer or context parameter in `RenderFX(int, XMMATRIX&)`.
  GL-prefixed structs (`GLParticle`, `GLStar`, `GLTunnelRing`, `GLWarpTunnelData`, etc.) avoid ODR
  violations with DX types. All fade, callback, tween, and `SaveAndSuspendFXForScene` /
  `RestoreFXAfterScene` logic mirrored from `VKFXManager`.
- **`OpenGLRenderFrame.cpp`** — full `OpenGLRenderer::RenderFrame()` pipeline: safety guards →
  exclusive lock → 8-frame weighted delta-time average → `glClear` → 3D scene switch
  (`RenderGamePlay`, `RenderIntroMovie`) → 2D overlay → `fxManager.Render2D()` →
  `fxManager.Render()` → FPS text → GUI → `SwapBuffers`. Inline `RenderGamePlay(float dt)` uploads
  DirectXMath view/proj/model matrices as shader uniforms and iterates `models[i]`. Inline
  `RenderIntroMovie()` uploads video frames to an OpenGL texture via `glTexImage2D`.
- **`OpenGL_IOStreamThread.h/.cpp`** — `OpenGLRenderer::LoaderTaskThread()` with full scene-switch
  structure matching the Vulkan loader thread: `SCENE_EXPERIMENT`, `SCENE_INTRO`,
  `SCENE_GAMETITLE`, `SCENE_LOAD_MP3`, `SCENE_GAMEPLAY`, `SCENE_GAMEOVER`. All `config.myConfig.displayMode`
  camera-setup switch branches (Windowed / Borderless / Fullscreen) replicated from the DX11 pattern.
- **`OpenGLRenderer.h/.cpp`** — all previously missing implementations added:
  - **Context creation** — `CreateOpenGLContext()` with `wglCreateContextAttribsARB` WGL 3.3 Core
    Profile; `CS_OWNDC` required (enforced in `main.cpp` window class registration).
  - **Shader system** — `CompileShader`, `CreateShaderProgram`, `LoadShaders`; GLSL source strings
    for 2D orthographic quad (`k_2dVertGLSL`/`k_2dFragGLSL`) and 3D model pipeline
    (`k_3dVertGLSL`/`k_3dFragGLSL`); `m_2dVAO`/`m_2dVBO` (GL_DYNAMIC_DRAW, 6 vertices, 4 floats).
  - **Texture loading** — `LoadTexture` via GDI+ `Gdiplus::Bitmap → LockBits → glTexImage2D`;
    `RenderTextToTexture` via GDI `CreateCompatibleDC → DrawTextW → DIBSection → BGRA→RGBA →
    glTexImage2D`.
  - **2D rendering** — `Render2DQuad` (orthographic projection, tinted textured quad),
    `Blit2DObject`, `Blit2DObjectToSize`, `Blit2DObjectAtOffset`, `Blit2DWrappedObjectAtOffset`,
    `DrawRectangle`, `DrawTexture`, `DrawVideoFrame`.
  - **Text drawing** — `DrawMyText` (×2 overloads), `DrawMyTextWithFont`, `DrawMyTextCentered`,
    `GetCharacterWidth` (×2), `CalculateTextWidth`, `CalculateTextHeight`.
  - **Display mode** — `SetFullScreen`, `SetFullExclusive`, `SetWindowedScreen` with WGL vsync via
    `wglSwapIntervalEXT`.
  - All `Cleanup`, `CleanupTextures`, `Resize`, `WaitForGPUToFinish`, `WaitToFinishThenPauseThread`
    methods fully implemented.
- **`CMakeLists.txt`** — `OpenGLFXManager.cpp`, `OpenGLRenderFrame.cpp`, `OpenGL_IOStreamThread.cpp`
  added to `SOURCES`.
- **`CrossPlatformGameEngine.vcxproj`** — `ClCompile` entries for the three new `.cpp` files;
  `ClInclude` entries for `OpenGLFXManager.h` and `OpenGL_IOStreamThread.h`.
- **`main.cpp`** — `#include "DX_FXManager.h"` replaced with a three-way conditional:
  `__USE_OPENGL__ → OpenGLFXManager.h`, `__USE_VULKAN__ → VULKAN_FXManager.h`,
  else `DX_FXManager.h`. Global `fxManager` declaration similarly guarded to instantiate
  `GLFXManager`, `VKFXManager`, or `FXManager` based on the active build configuration.
- *See: [`OpenGLFXManager.h`](OpenGLFXManager.h), [`OpenGLFXManager.cpp`](OpenGLFXManager.cpp),
  [`OpenGLRenderFrame.cpp`](OpenGLRenderFrame.cpp), [`OpenGL_IOStreamThread.h`](OpenGL_IOStreamThread.h),
  [`OpenGL_IOStreamThread.cpp`](OpenGL_IOStreamThread.cpp), [`OpenGLRenderer.h`](OpenGLRenderer.h),
  [`OpenGLRenderer.cpp`](OpenGLRenderer.cpp), [`CMakeLists.txt`](CMakeLists.txt),
  [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj), [`main.cpp`](main.cpp)*

**May 17, 2026** - OpenGL renderer abstract class fix and renderer-appropriate windowing:

- **Bug fix — `OpenGLRenderer` was an uninstantiable abstract class**: Five pure virtual methods
  from `Renderer` were not overridden, causing `error C2259` when `RendererFactory.cpp` tried to
  `make_shared<OpenGLRenderer>()`.
  - **`GetDevice()` / `GetDeviceContext()` / `GetSwapChain()`**: Added as inline overrides returning
    `nullptr` — OpenGL has no DX device/context/swapchain equivalents.
  - **`WaitToFinishThenPauseThread()`**: Added declaration in header and implementation in `.cpp`;
    calls `WaitForGPUToFinish()` (which issues `glFinish()` under the render mutex) then
    `threadManager.PauseThread(THREAD_RENDERER)` — matching the Vulkan pattern.
  - **`Blit2DColoredPixel(int, int, float, XMFLOAT4)`**: Signature in `OpenGLRenderer.h` was
    `Vector4 color` (no match for base class `XMFLOAT4`) so it never overrode the pure virtual.
    Changed to `XMFLOAT4 color override`. Implementation uses the OpenGL fixed-function
    `glColor4f` / `glPointSize` / `glBegin(GL_POINTS)` with Y-axis flip for top-left origin.
  - `WaitForGPUToFinish()` was declared in the header but unimplemented; added a body that
    locks `s_renderMutex` and calls `glFinish()`.
- **Renderer-appropriate window class setup**: OpenGL WGL requires the window's device context
  to be persistent (not recycled between `GetDC`/`ReleaseDC` pairs). Added a runtime check at
  window-class registration: if the active renderer is OpenGL, `CS_OWNDC` is OR'd into `wc.style`
  so WGL gets a stable DC throughout the session. DX11/DX12/Vulkan are unaffected.
- **`bIsMinimized` guard extended to all renderers**: `WM_SIZE` handlers for `SIZE_MINIMIZED`
  and `SIZE_RESTORED` were gated by `#if defined(__USE_DIRECTX_11__)`. Since `bIsMinimized` is a
  member of the `Renderer` base class, the guard was removed — all renderer backends now track
  the minimized state correctly.
- *See: [`OpenGLRenderer.h`](OpenGLRenderer.h), [`OpenGLRenderer.cpp`](OpenGLRenderer.cpp),
  [`main.cpp`](main.cpp)*

**May 17, 2026** - OpenGL and Vulkan GLM-based camera classes added:

- **`OpenGLCamera.h/.cpp`** — full `Camera` class for OpenGL rendering, converted from `DXCamera.h/.cpp`:
  - All DirectX Math types replaced with GLM equivalents: `XMFLOAT3` → `glm::vec3`, `XMMATRIX` → `glm::mat4`,
    `XMVECTOR` → `glm::vec3`/`glm::vec4`, `XMLoadFloat3`/`XMStoreFloat3` removed entirely.
  - View matrix via `glm::lookAt(eye, center, up)`; projection via `glm::perspective(fovY, aspect, near, far)` —
    standard right-handed OpenGL convention, depth range [-1, 1].
  - DX row-major combined rotations (`rotX * rotY * rotZ`) reversed to GLM column-major order
    (`rotZ * rotY * rotX`) in `RotateXYZ` and `CalculateRotatedPosition`.
  - `FAST_MATH.FastDistance(XMFLOAT2, XMFLOAT2)` calls replaced with `glm::length(glm::vec2(...))` — avoids
    the DX-type dependency.
  - `GetFieldOfView` / `GetNearPlane` / `GetFarPlane` return cached member values directly rather than
    extracting them from the projection matrix (simpler, no GLM element parsing needed).
  - Guarded by `#ifdef __USE_OPENGL__`; includes `<glm/glm.hpp>`, `<glm/gtc/matrix_transform.hpp>`,
    `<glm/gtc/type_ptr.hpp>`.

- **`VulkanCamera.h/.cpp`** — identical logic to `OpenGLCamera` but with Vulkan-specific projection:
  - Private `MakeVulkanProjection(fovYRadians, aspect, near, far)` helper calls `glm::perspective` then
    applies `proj[1][1] *= -1.0f` to flip the Y axis for Vulkan NDC (Y points down in Vulkan).
  - All projection matrix creation routes through `MakeVulkanProjection` so the Y-flip is consistently
    applied everywhere, including `SetupDefaultCamera`, `UpdateProjectionMatrix`, and `UpdateResolution`.
  - Depth range is [0, 1] (Vulkan convention).
  - Guarded by `#ifdef __USE_VULKAN__`.

- **Build system integration**: Both new `.cpp` files added to `SOURCES` in `CMakeLists.txt` (OpenGLCamera
  near OpenGLRenderer; VulkanCamera near VULKAN_Renderer). Corresponding `ClCompile` and `ClInclude`
  entries added to `CrossPlatformGameEngine.vcxproj`.
- *See: [`OpenGLCamera.h`](OpenGLCamera.h), [`OpenGLCamera.cpp`](OpenGLCamera.cpp),
  [`VulkanCamera.h`](VulkanCamera.h), [`VulkanCamera.cpp`](VulkanCamera.cpp),
  [`CMakeLists.txt`](CMakeLists.txt), [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj)*

**May 17, 2026** - Build system handle-release on cancelled/failed builds:

- **`cmake-build.bat`** — added `:killcompiler` subroutine (`taskkill /F /IM cl.exe` +
  `taskkill /F /IM mspdbsrv.exe`, errors suppressed). Called at two points:
  - **Before cmake runs** — releases handles left by any previously cancelled build
    (primary guarantee; Ctrl+C in a batch file cannot be trapped).
  - **At `:error`** — releases handles from the build that just failed or was cancelled.
- **`Directory.Build.targets`** — added `KillStaleCompilerProcesses` MSBuild target
  (`BeforeTargets="ClCompile;Build"`) that runs the same two `taskkill` commands before
  VS 2022 spawns new `cl.exe` instances. Ensures any orphaned handles from a previous
  VS cancel are released at the start of every IDE build. `IgnoreExitCode="true"` prevents
  a non-zero taskkill result (nothing running) from failing the build.
- *See: [`cmake-build.bat`](cmake-build.bat), [`Directory.Build.targets`](Directory.Build.targets)*

**May 17, 2026** - Renderer.h camera include fix — conditional include by active renderer:

- **Bug fix — `error C3646: 'myCamera': unknown override specifier`**: `Renderer.h` unconditionally
  included `DXCamera.h`, but `DXCamera.h` wraps its `Camera` class inside
  `#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)`. When only `__USE_OPENGL__` is
  active the `Camera` type is never emitted, making `Camera myCamera` on line 276 an unknown type.
  - Replaced the hard-coded `#include "DXCamera.h"` with a three-way conditional block that selects
    the correct camera header based on the active renderer define:
    `__USE_DIRECTX_11__` / `__USE_DIRECTX_12__` → `DXCamera.h`;
    `__USE_OPENGL__` → `OpenGLCamera.h`;
    `__USE_VULKAN__` → `VulkanCamera.h`.
- *See: [`Renderer.h`](Renderer.h)*

**May 17, 2026** - Cross-platform math types, models, shaders, and build system — full multi-renderer pass:

- **Cross-platform math type aliases** (`Includes.h`): For non-DX builds (`__USE_OPENGL__` /
  `__USE_VULKAN__`), added a type-alias block that maps all DirectX Math types to engine-native
  equivalents so every struct and function using them compiles without per-callsite guards:
  - `#include "Vectors.h"` (provides `Vector2`, `Vector3`, `Vector4` with identical float layout)
  - `struct Matrix4x4` — simple 4×4 identity matrix for OpenGL/Vulkan
  - `using XMFLOAT2 = Vector2; using XMFLOAT3 = Vector3; using XMFLOAT4 = Vector4;`
  - `using XMMATRIX = Matrix4x4; using XMVECTOR = Vector4;`
  - `namespace DirectX {}` — empty namespace so `using namespace DirectX;` compiles without pulling DX symbols

- **`ConstantBuffer.h` cross-platform**: Changed from unconditional `<d3d11.h>` / `<DirectXMath.h>`
  includes to `#include "Includes.h"` base, with all DX-specific includes guarded by
  `#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)`. Struct layouts unchanged.

- **`Renderer.h` XMFLOAT4 guard fix**: `Blit2DColoredPixel` was inside `#if defined(_WIN64)`
  (too broad — XMFLOAT4 undefined on OpenGL builds). Split into:
  DX11/DX12 → `XMFLOAT4 color`; OpenGL/Vulkan → `const Vector4& color`.

- **`Models.h` cross-platform rewrite**: Removed unconditional DX11 includes and
  `using namespace DirectX;`. Added conditional platform blocks:
  - `Texture`: `GetSRV()` inside DX11 guard; `GLuint GetTextureID()` for OpenGL; `VkImageView GetImageView()` for Vulkan
  - `ModelInfo`: common transform fields always present; matrices (`worldMatrix`/`viewMatrix`/`projectionMatrix`)
    use `XMMATRIX` (DX) or `Matrix4x4` (GL/VK); separate GPU fields per renderer
  - `Model::Render()`: takes `ID3D11DeviceContext*` on DX; `float deltaTime` on OpenGL/Vulkan
  - `IsActive()`, `HasGeometry()`, `GetWorldMatrix()` inline helpers added for Vulkan render frame

- **`Lights.h` cross-platform**: `using namespace DirectX;` now guarded by DX-only block. `LightStruct`
  field types (`XMFLOAT3`) resolve via aliases on non-DX builds — no layout change.

- **`BlenderImports.h/.cpp` cross-platform**: `GLTF_DEFAULT_FLIP` constexpr now renderer-conditional
  (`FLIP_Z` for DX, `FLIP_Y` for Vulkan, `FLIP_NONE` for OpenGL). `ConvertNodeMatrix` signature
  conditional (`XMMATRIX` for DX; `Matrix4x4` for GL/VK). `.cpp` implementation splits
  `XMMatrixScaling` (DX-only) from manual row-negation implementation for OpenGL/Vulkan.

- **`MathPrecalculation.h` and `Physics.h` cross-platform**: Guarded `#include <DirectXMath.h>`
  and `using namespace DirectX;` under `__USE_DIRECTX_11__ || __USE_DIRECTX_12__`. All struct
  fields resolve via Includes.h type aliases on non-DX builds.

- **`GUIManager.h` renderer-aware forward declaration**: Renderer class forward declaration
  changed from always-DX11 to conditional: `DX11Renderer` / `DX12Renderer` / `OpenGLRenderer` / `VulkanRenderer`
  based on the active `__USE_*` define.

- **OpenGL model infrastructure** — two new files:
  - **`OpenGLModels.h`**: `OpenGLModelUtils` namespace (texture/shader helpers); `OpenGLModelBuffers`
    struct (VAO/VBO/EBO/UBO handles with `Upload()`/`Destroy()`); `OpenGLMaterialUniforms`
    (`Apply()`/`ApplyTransform()` for uniform uploads). Guarded by `__USE_OPENGL__`.
  - **`OpenGLModels.cpp`**: stb_image texture loading; VAO vertex attrib layout (stride=44:
    position/normal/texCoord/tangent at locations 0–3); `glGetUniformLocation`-based material
    and transform uploads.

- **Vulkan model infrastructure** — two new files:
  - **`VulkanModels.h`**: `VulkanModelUtils` namespace (buffer/image/sampler helpers);
    `VulkanModelBuffers` struct (all `VkBuffer`/`VkImage`/`VkPipeline`/`VkDescriptorSet` handles
    with `Upload()`/`Destroy()`). Guarded by `__USE_VULKAN__`.
  - **`VulkanModels.cpp`**: staging-buffer upload pattern; `FindMemoryType`; stb_image integration;
    shaderc GLSL→SPIR-V or `.spv` file fallback for shader modules.

- **OpenGL GLSL shaders** — two new shader files:
  - **`Assets/Shaders/ModelVertex.glsl`** (GLSL 330 core): OpenGL vertex shader; std140 UBO binding=0;
    vertex locations 0–3; `inverse3x3` shear-safe normal matrix; outputs 6 varyings for the fragment stage.
  - **`Assets/Shaders/ModelPixel.glsl`** (GLSL 330 core): OpenGL PBR fragment shader; combined
    sampler2D/samplerCube bindings; std140 UBOs at bindings 0–5; full Cook-Torrance BRDF
    (Fresnel-Schlick, GGX distribution, Smith geometry); all 10 debug modes; Reinhard tone-mapping
    + gamma correction. DirectX→OpenGL G-channel normal map correction applied.

- **Vulkan GLSL/SPIR-V shaders** — two new shader files:
  - **`Assets/Shaders/ModelVertex.vert`** (GLSL 450): Vulkan vertex shader; `set=0,binding=0` UBO;
    Vulkan clip-space fix: `clipPos.y = -clipPos.y; clipPos.z = (clipPos.z + clipPos.w) * 0.5`.
  - **`Assets/Shaders/ModelPixel.frag`** (GLSL 450): Vulkan PBR fragment shader; `set=0` for
    UBOs (bindings 0–5); `set=1` for combined image samplers (bindings 0–5); same PBR logic as
    OpenGL version.

- **CMake shader build integration** (`CMakeLists.txt`):
  - HLSL→CSO: `fxc.exe` (Windows SDK) for DirectX shaders — unchanged, already present
  - OpenGL GLSL: `copy_glsl_shader` macro copies `.glsl` files POST_BUILD (runtime-loaded via opengl32.lib)
  - Vulkan SPIR-V: `compile_vulkan_shader` macro uses `glslangValidator` from Vulkan SDK PRE_BUILD;
    falls back to raw `.vert`/`.frag` copy if `glslangValidator` absent (shaderc runtime compilation)
  - CMake status messages identify which tool compiles which shaders
  - `OpenGLModels.cpp` and `VulkanModels.cpp` added to `SOURCES`

- **Build system** (`CrossPlatformGameEngine.vcxproj`):
  - `<ClCompile>` entries added: `OpenGLModels.cpp`, `VulkanModels.cpp`
  - `<ClInclude>` entries added: `OpenGLModels.h`, `VulkanModels.h`

- *See: [`Includes.h`](Includes.h), [`ConstantBuffer.h`](ConstantBuffer.h), [`Renderer.h`](Renderer.h),
  [`Models.h`](Models.h), [`Lights.h`](Lights.h), [`BlenderImports.h`](BlenderImports.h),
  [`BlenderImports.cpp`](BlenderImports.cpp), [`MathPrecalculation.h`](MathPrecalculation.h),
  [`Physics.h`](Physics.h), [`GUIManager.h`](GUIManager.h),
  [`OpenGLModels.h`](OpenGLModels.h), [`OpenGLModels.cpp`](OpenGLModels.cpp),
  [`VulkanModels.h`](VulkanModels.h), [`VulkanModels.cpp`](VulkanModels.cpp),
  [`Assets/Shaders/ModelVertex.glsl`](Assets/Shaders/ModelVertex.glsl),
  [`Assets/Shaders/ModelPixel.glsl`](Assets/Shaders/ModelPixel.glsl),
  [`Assets/Shaders/ModelVertex.vert`](Assets/Shaders/ModelVertex.vert),
  [`Assets/Shaders/ModelPixel.frag`](Assets/Shaders/ModelPixel.frag),
  [`CMakeLists.txt`](CMakeLists.txt), [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj)*

---

## Future Development

### **PRODUCTION READINESS APPROACHING**
The DirectX 11 system is nearing **GAMING PRODUCTION READINESS** for Windows 10 SP1+ 64-bit systems.

### **Project Mission Statement**

1. **FOSS Contribution**: Supporting Free Open Source Software with professional-grade development
2. **Developer-Friendly Design**: Comprehensive documentation and example usage for rapid learning
3. **Production Focus**: Emphasis on game development rather than engine complexity where I have removed as much of the hard work out for the developer.
4. **Future Vision**: Next-generation programmatic gaming engine

### **Developer Guidelines**

- **Testing Protocol**: Always test the complete GitHub base with full clone/pull before starting projects
- **Issue Reporting**: Email `https://ultimanium.com/index.php?action=contact-development` with subject "CPGE Problem - [Brief Description]"
- **Debug Information**: Include compile/runtime logs from DEBUG builds
- **License**: Perpetual MIT License with lifetime updates

---

## Important Notes

### **⚠️ Development Warning**
Since this is early-stage days of development (WIP), major reconstruction may occur and some system classes need more extending to suit other OS Platforms. **Use at your own risk.**

### **📋 Current Priority Tasks**

**May 27, 2025** - Multi-renderer implementation:
- OpenGL Renderer for Windows/Linux/MacOS/Android (WIP)
- Vulkan Renderer for Windows/Linux/Android (WIP)

**March 16, 2025** - Platform integrations:
- Steam Class implementation (WIP)
- GooglePlay interface for Android (WIP)
- AppStore interface for iOS (WIP)

**May 25, 2025** - Optimization:
- Code optimizations and refactoring

**June 7, 2025** - Sub-Systems Extending:
- Some System Classes need more work to suit other platforms.
---

## Technical Requirements

- **Operating System**: Windows 10 SP1+ (64-bit), Linux, Android, iOS or MacOS (will fill this in more when I know more on certainty!)
- **Development Environment**: Visual Studio 2019/2022 / CMAKE / VSCode
- **Language Standard**: C++17 Compliant
- **Graphics APIs**: DirectX 11/12, OpenGL, Vulkan with a minimum or equivilant NVIDIA GTX-960M card.
- **Architecture**: Win x64, Linux, Android, iOS and MacOS only for now!

---

*End of File*
