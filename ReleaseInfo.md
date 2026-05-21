# DirectX 11 & 12, OpenGL & Vulkan Game Engine

**Cross Platform Gaming Engine by Daniel J. Hobson**  
*Melbourne, Australia 2023-2026*

*Current Build Version: v0.0.1276 — May 21, 2026*

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

**May 16, 2026** - Console Output Window, WarpDotTunnel enhancements and spin fix:

**Console Output Window (F8 toggle):**

- **ConsoleWindow class** — lightweight scene-aware OSD rendered via `Renderer` (no GUIManager dependency). 100-line circular buffer (`std::deque<ConsoleLine>`), thread-safe `AddLine()`.
- **14-line visible area**: newest line at bottom; vertical scrollbar with proportional thumb. Page Up / Page Down (3 lines/press) + mouse wheel capture + scrollbar click-to-jump. Scrollbar geometry cached per frame for zero-cost hit-testing.
- **Font**: `clamp(screenHeight / 108, 8, 12)` — ~10pt at 1080p, scales with resolution.
- **Scene restriction**: only rendered in `SCENE_GAMETITLE` / `SCENE_GAMEPLAY`; suppressed automatically during `bSceneSwitching`. F8 handler in `KBHandlersCode.cpp` ignores all other scenes.
- **Layout**: width 80 % of screen / max 1400 px; bottom margin 50 px to clear taskbar; long lines extend right (no wrap).
- **Debug integration** (`_DEBUG` only): all `Debug::` paths forward to console — `LOG_WARNING` → yellow, `LOG_ERROR`/`LOG_CRITICAL` → orange, all others → white. Release builds unchanged.
- *See: [`ConsoleWindow.h`](ConsoleWindow.h), [`ConsoleWindow.cpp`](ConsoleWindow.cpp), [`Debug.cpp`](Debug.cpp), [`KBHandlersCode.cpp`](KBHandlersCode.cpp), [`DXRenderFrame.cpp`](DXRenderFrame.cpp)*

**WarpDotTunnel visual and motion enhancements:**

- **8-step sequential gray ramp** (`colorStep = ringIndex % 8`): values `{0.08…1.0}` cycle across rings so the full dark-to-white gradient is always simultaneously visible. `kGraySteps`, `colorStep` added to both DX and VK structs.
- **Quartic acceleration, 1.0× minimum**: `1.0 + t⁴ × 10.0` (forward) / `1.0 + t⁴ × 6.0` (reverse) — far rings always visibly moving (80 u/s), near rings surge to 880 u/s.
- **Per-ring birth offset** (`bornCx`/`bornCy`): assigned at spawn from a circular sin/cos wave (X and Y 90° out of phase); rings travel dead-straight from birth position to camera — helical corridor feel. `kSideWaveRadius`/`kSideWaveSpeed` constants: DX `80.0 / 0.85 rad/s`, VK `60.0 / 0.50 rad/s`.
- **Camera look-ahead raised to 20 rings**: tracks the 20th-nearest ring, smoothing jitter from fast near-rings.
- **Exponential ease on look target** (`smoothLookTarget`): framerate-independent lerp `alpha = 1 − exp(−kCameraSmooth × dt)`. DX `kCameraSmooth = 4.0`, VK `3.0`. Seeded to `(startX, startY, farZ)` at init — no startup snap.
- **Bug fix — reverse-mode spin direction inverted**: spin delta negated when `reverseTravel = true` (`spinDelta = (reverseTravel ? -spinSpeed : spinSpeed) * dt`). `Clockwise`/`AntiClockwise` now consistent regardless of travel direction. Applied identically in DX and VK.
- *See: [`DX_FXManager.h`](DX_FXManager.h), [`DX_FXManager.cpp`](DX_FXManager.cpp), [`VULKAN_FXManager.h`](VULKAN_FXManager.h), [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp)*

**May 17, 2026** - Full multi-renderer implementation pass — OpenGL/Vulkan infrastructure, build system fixes, and zero-error OpenGL build achieved (v0.0.1219):

- **GUIManager Z-order input routing**: `GUIWindow::zOrder` auto-assigned from a monotonic counter at `CreateMyWindow`. Render snapshot sorted ascending; input routed to topmost window only; occluded windows have hover flags cleared; in-progress drags continue receiving `HandleMouseMove` until release.

- **Video tab renderer selector**: Always visible regardless of compiled backends; `rendererType` added to config checksum. `__USE_OPENGL__`/`__USE_VULKAN__` uncommented in `Includes.h`. Replaced `RENDERER_NAMES[]`/`RENDERER_MAX` with compile-time `AVAILABLE_RENDERERS[]` list; slider hidden when only one backend present. GLEW include order fixed (`glew.h` before `<GL/gl.h>`); `glu32.lib` added.

- **Vulkan SDK auto-detection**: `CMakeLists.txt` `find_package(Vulkan)` with drive-root fallback scan and `FATAL_ERROR` if not found. `$(VULKAN_SDK)\Include` and `$(VULKAN_SDK)\Lib` added to `CrossPlatformGameEngine.vcxproj` for both configurations.

- **Build error fixes (rounds 1 & 2)**: `VkAspectFlags` → `VkImageAspectFlags`; `RENDERER_NAME` ODR guard added; `OpenGLRenderer::Resize` return type fixed to `bool`; `Model::IsActive`/`HasGeometry`/`GetWorldMatrix` inlines added; `SetThread`+`StartThread` pattern corrected; GLEW C4005 suppressed. Stale `"VulkanRenderer.h"` include (resolving to old `F:\Projects\C++\CPGE` project, causing 40+ C2374 redefinitions) corrected to `"VULKAN_Renderer.h"`.

- **Full OpenGL renderer implementation**: `OpenGLFXManager.h/.cpp` — all FX types ported (`ColorFader`, `Starfield`, `WarpDotTunnel`, `ParticleExplosion`, `Scroller`, all `TextScroller` variants) with GL-prefixed structs to avoid ODR violations. `OpenGLRenderFrame.cpp` — full pipeline with 8-frame delta-time average, scene switch, 2D overlay, FX, GUI, SwapBuffers. `OpenGL_IOStreamThread.h/.cpp` — all scenes with display-mode camera branches. `OpenGLRenderer.h/.cpp` — WGL 3.3 Core Profile context, shader compilation, GDI+ texture loading, full 2D/text/display-mode API, all abstract-class overrides. `main.cpp` fxManager guarded for three backends. All files added to `CMakeLists.txt` and vcxproj.

- **OpenGL abstract class and windowing fixes**: Five missing pure-virtual overrides added (`GetDevice`/`GetDeviceContext`/`GetSwapChain` returning `nullptr`; `WaitToFinishThenPauseThread`; `Blit2DColoredPixel` with correct `XMFLOAT4` signature). `CS_OWNDC` OR'd into window class style for OpenGL. `bIsMinimized` guard extended to all renderers.

- **GLM-based camera classes**: `OpenGLCamera.h/.cpp` — right-handed, `glm::lookAt`/`glm::perspective`, column-major rotation order, guarded by `__USE_OPENGL__`. `VulkanCamera.h/.cpp` — identical with `proj[1][1] *= -1.0f` Y-flip and [0,1] depth range, guarded by `__USE_VULKAN__`. Both added to build systems.

- **Build system handle-release**: `cmake-build.bat` `:killcompiler` subroutine kills `cl.exe`/`mspdbsrv.exe` before cmake and on error. `Directory.Build.targets` `KillStaleCompilerProcesses` MSBuild target (`BeforeTargets="ClCompile;Build"`) with `IgnoreExitCode="true"`.

- **Cross-platform math/models/shaders**: `Includes.h` type aliases (`XMFLOAT2/3/4` → `Vector2/3/4`, `XMMATRIX` → `Matrix4x4`, empty `namespace DirectX`). `Renderer.h` camera include made three-way conditional (`DXCamera.h`/`OpenGLCamera.h`/`VulkanCamera.h`). `ConstantBuffer.h`, `Models.h`, `Lights.h`, `BlenderImports.h/.cpp`, `MathPrecalculation.h`, `Physics.h`, `GUIManager.h` all made renderer-conditional. `OpenGLModels.h/.cpp` and `VulkanModels.h/.cpp` added (VAO/VBO/EBO/UBO and Vulkan buffer infrastructure). OpenGL PBR shaders (`ModelVertex.glsl`, `ModelPixel.glsl` — Cook-Torrance BRDF, 10 debug modes, Reinhard tone-mapping) and Vulkan shaders (`ModelVertex.vert`, `ModelPixel.frag`) created. CMake shader build macros added for GLSL copy and SPIR-V compilation.

- **Zero-error OpenGL build**: `Matrix4x4` extended with `_11`–`_44` union fields; `operator*`/`XMFLOAT4X4` moved into stubs guard (GLTFAnimator conflict fix); additional math stubs added. `SceneManager.cpp` vertex and camera accesses guarded per-renderer. `GUIWindows.cpp`/`KBHandlersCode.cpp` `fxManager` extern made renderer-conditional. `ScreenRecorder.cpp` DX11 frame-capture guarded. `MoviePlayer.h` `GetCurrentFrameRGBA` stub added. `Renderer.h` `IMG_BACKGROUND` enum entry added. **Result: zero compile and link errors on `__USE_OPENGL__` path.**

- *See: [`GUIManager.h`](GUIManager.h), [`GUIManager.cpp`](GUIManager.cpp), [`GUIConfigWindow.cpp`](GUIConfigWindow.cpp), [`Configuration.cpp`](Configuration.cpp), [`Includes.h`](Includes.h), [`CMakeLists.txt`](CMakeLists.txt), [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj), [`OpenGLRenderer.h`](OpenGLRenderer.h), [`OpenGLRenderer.cpp`](OpenGLRenderer.cpp), [`OpenGLFXManager.h`](OpenGLFXManager.h), [`OpenGLFXManager.cpp`](OpenGLFXManager.cpp), [`OpenGLRenderFrame.cpp`](OpenGLRenderFrame.cpp), [`OpenGL_IOStreamThread.h`](OpenGL_IOStreamThread.h), [`OpenGL_IOStreamThread.cpp`](OpenGL_IOStreamThread.cpp), [`OpenGLCamera.h`](OpenGLCamera.h), [`OpenGLCamera.cpp`](OpenGLCamera.cpp), [`VulkanCamera.h`](VulkanCamera.h), [`VulkanCamera.cpp`](VulkanCamera.cpp), [`OpenGLModels.h`](OpenGLModels.h), [`OpenGLModels.cpp`](OpenGLModels.cpp), [`VulkanModels.h`](VulkanModels.h), [`VulkanModels.cpp`](VulkanModels.cpp), [`Models.h`](Models.h), [`Renderer.h`](Renderer.h), [`SceneManager.cpp`](SceneManager.cpp), [`GUIWindows.cpp`](GUIWindows.cpp), [`KBHandlersCode.cpp`](KBHandlersCode.cpp), [`ScreenRecorder.cpp`](ScreenRecorder.cpp), [`MoviePlayer.h`](MoviePlayer.h), [`cmake-build.bat`](cmake-build.bat), [`Directory.Build.targets`](Directory.Build.targets), [`RendererFactory.cpp`](RendererFactory.cpp), [`main.cpp`](main.cpp)*

**May 18, 2026** - Multi-renderer build hardening, feature additions, and OpenGL runtime fixes (v0.0.1246):

**Build fixes:**

- **DX11 linker — `DX_FXManager.cpp` compiled to nothing (18 unresolved symbols)**: `DX_FXManager.cpp`
  began with `#if defined(__USE_DIRECTX_11__)` before any `#include`. `Includes.h` (which defines
  `__USE_DIRECTX_11__`) is inside that guard, so the preprocessor never saw the define and the entire
  translation unit compiled to nothing.
  - **CMake fix**: Added `__USE_DIRECTX_11__` to `target_compile_definitions` as a `/D` flag so the
    guard evaluates correctly before any includes are processed.
  - **MSVS 2022 fix (source level)**: Moved `#include "Includes.h"` to the very top of
    `DX_FXManager.cpp` before the guard — matches the pattern in `OpenGLFXManager.cpp` and
    `VULKAN_FXManager.cpp`, making the TU self-sufficient regardless of build system.
  - **MSVS 2022 fix (vcxproj level)**: `__USE_DIRECTX_11__` added to `<PreprocessorDefinitions>` for
    both configurations — belt-and-suspenders so the guard works even if the source-level fix is reverted.
  - **C4005 fix**: Wrapped the `#define __USE_DIRECTX_11__` in `Includes.h` with `#ifndef` so it only
    fires when the build system has not already injected the flag. Zero warnings on clean rebuild.

- **PKEY linker error (`unresolved external symbol PKEY_AudioEndpoint_FormFactor`)**: `PKEY_*` constants
  require `INITGUID` to be defined before any transitive pull-in of `<propkeydef.h>`. Moved
  `#include <initguid.h>` to the first include inside the Windows block of `main.cpp` so
  `DEFINE_PROPERTYKEY` emits the full `DECLSPEC_SELECTANY` symbol definition. No external library required.

- **Dual-renderer symbol collision (`Camera` / `CameraJumpHistoryEntry` / `CameraResizeState` redefined)**:
  `Includes.h` had an unconditional `#define __USE_OPENGL__`, causing both DX and OpenGL camera headers to
  be included simultaneously → C2011/C2086 redefinition cascade. Fixed by wrapping all four renderer
  fallback `#define` lines in a single `#if !defined(...)` guard so they only fire when no renderer
  is pre-defined by the build system.

- **OpenGL linker warnings eliminated (`LNK4217`/`LNK4286`/`LNK4098`/`LNK4099`)**:
  - **LNK4217/LNK4286** — `<GL/glew.h>` included without `GLEW_STATIC`; GLEW defaulted to
    `__declspec(dllimport)`. Added `#ifndef GLEW_STATIC / #define GLEW_STATIC` before the include
    in `Includes.h`; also added to `target_compile_definitions` (CMake) and vcxproj `<PreprocessorDefinitions>`.
  - **LNK4098** — prebuilt `glew32s.lib` uses `/MT`; debug builds use `/MTd`. Added
    `/NODEFAULTLIB:LIBCMT` to debug link options in both CMakeLists.txt and vcxproj.
  - **LNK4099** — `glew32s.lib` carries no PDB. Added `/ignore:4099` to link options in both build systems.

- **Build-time SDK diagnostics** — `PrintSDKDiagnostics` MSBuild target added to vcxproj
  (`BeforeTargets="ClCompile"`). Emits a formatted block at the start of every build showing Vulkan SDK
  path/header/lib, shader compiler locations (`glslc.exe`, `glslangValidator.exe`, `dxc.exe`,
  `d3dcompiler`), and OpenGL/GLEW header and lib checks. MSBuild warnings fire if `VULKAN_SDK` is
  unset or `glew.h` is missing.

- *See: [`DX_FXManager.cpp`](DX_FXManager.cpp), [`Includes.h`](Includes.h), [`main.cpp`](main.cpp),
  [`CMakeLists.txt`](CMakeLists.txt), [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj)*

**New features:**

- **Audio device detection at startup** — `DetectAndLogAudioDevices()` added to `main.cpp` (Windows
  only). Runs after COM initialisation, before any audio subsystem starts. Queries all four WASAPI
  endpoints (Playback Default/Communications, Capture Default/Communications) and writes friendly names
  and hardware form factors to the debug log. Audio routing confusion is immediately diagnosable from
  the launch log. Added `#include <functiondiscoverykeys_devpkey.h>` for `PKEY_Device_FriendlyName`.

- **ScreenRecorder mic capture endpoint fix** — `InitMicCapture()` now prefers `eCommunications`
  capture over `eConsole`. Windows assigns a headset mic to `eCommunications` and the built-in mic to
  `eConsole`; the old code always captured `eConsole`, silently ignoring plugged-in headsets.
  Now tries `eCommunications` first, falls back to `eConsole` only when no separate communications
  capture device exists.

- **Dynamic executable naming** — renderer-prefixed output names across all build systems:
  - `#define GAME_NAME "CPGE"` added to `Includes.h` as the single authoritative base name.
  - **vcxproj**: `<GameName>` and `<ActiveRenderer>` UserMacros added. Set `ActiveRenderer` to `DX11`,
    `DX12`, `OpenGL`, or `Vulkan` to switch output name and renderer define in one place. Outputs:
    `DXCPGE.exe` (default), `OpenGLCPGE.exe`, `VulkanCPGE.exe`. `<PreprocessorDefinitions>` updated
    from hardcoded `__USE_DIRECTX_11__` to `$(_RendererDefine)`.
  - **CMakeLists.txt**: `GAME_NAME` and `RENDERER` cache variables; output name set via
    `set_target_properties ... OUTPUT_NAME`. Platform-appropriate extensions applied automatically
    (`.exe` Windows, `.app` macOS/iOS, no extension Linux, library Android). `target_compile_definitions`
    uses `${RENDERER_DEFINE}`.

- **Renderer info overlay** — renderer name + build version displayed in the bottom-right corner on
  `SCENE_GAMETITLE`, `SCENE_GAMEPLAY`, `SCENE_INTRO`, `SCENE_GAMEOVER` (and `SCENE_EXPERIMENT` in Debug
  only). Suppressed during scene transitions. `USE_RENDERER_INFO = true` flag added to `Renderer.h`.
  Exact pixel positioning via `IDWriteTextLayout::GetMetrics()` for reliable right-edge alignment.
  Font size: `clamp(iOrigHeight / 72.0f, 10.0f, 16.0f)`. Debug FPS text rescaled to
  `clamp(height / 108, 8, 12)` (was hardcoded 10 pt). `#include "BuildInfo.h"` added to `DXRenderFrame.cpp`.

- **Config window — all renderers shown on Windows; Near/Far Plane sliders added**:
  - Renderer selector on Windows is now an unconditional 4-entry list (DirectX 11, 12, OpenGL, Vulkan)
    regardless of compiled backends; non-Windows platforms retain compile-time filtering.
  - Near Plane slider (Game Play tab): range 0.1 → 2.0, 2 dp. Writes `config.myConfig.nearPlane`.
  - Far Plane slider (Game Play tab): range 500 → 2000, integer. Writes `config.myConfig.farPlane`.

- *See: [`main.cpp`](main.cpp), [`ScreenRecorder.cpp`](ScreenRecorder.cpp), [`Includes.h`](Includes.h),
  [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj), [`CMakeLists.txt`](CMakeLists.txt),
  [`DXRenderFrame.cpp`](DXRenderFrame.cpp), [`Renderer.h`](Renderer.h), [`BuildInfo.h`](BuildInfo.h),
  [`GUIConfigWindow.cpp`](GUIConfigWindow.cpp)*

**Bug fixes:**

- **Bug fix — OpenGLCPGE.exe silent exit on startup (`rendererType` config mismatch)**:
  `GameConfig.cfg` persists `"rendererType": 0` (DirectX 11). `ValidateRendererForPlatform(0)` fell
  through to a hard-coded `return 0`; `CreateRendererInstance()` then hit `default: EXIT_FAILURE` and
  exited silently before any window appeared.
  - **Fix 1** — `ValidateRendererForPlatform` now returns the renderer compiled into this binary
    (`__USE_OPENGL__` → 2, `__USE_DIRECTX_12__` → 1, `__USE_VULKAN__` → 3, else 0).
  - **Fix 2** — `rendererType` is loaded raw first so checksum verification uses the value that was
    hashed on save; `ValidateRendererForPlatform` is called after the checksum block to avoid false
    tamper-detection resets.
  - **Fix 3** — `saveConfig()` writes `myConfig.rendererType` directly (already validated at load time)
    so the JSON value and checksum input are always identical.

- **Bug fix — no startup image (`LoadAllShaders` failure)**: `LoadAllShaders()` unconditionally
  attempted to load `ModelVertex.hlsl`/`ModelPixel.hlsl` via `ShaderManager::LoadShader`, which called
  `CompileGLSL()` on HLSL source and failed. This returned `false`, causing `main.cpp` to log
  `[CRITICAL]: Failed to load required shaders!` and exit before any scene rendered. Fixed by adding
  a `#if defined(__USE_OPENGL__)` early-return — the OpenGL renderer already compiles its own inline
  GLSL shaders in `OpenGLRenderer::LoadShaders()`.

- **Bug fix — access violation crash on exit (MoviePlayer atexit destructor)**: `moviePlayer` is a
  global (`main.cpp:180`). Its atexit destructor called `Cleanup()` → `Stop()` →
  `ThreadLockHelper(*m_threadManager, ...)`. By atexit time `threadManager` is already destroyed;
  dereferencing its internal mutex produced SEH 0xC0000005. Fixed by inlining the stop logic in
  `Cleanup()` (`m_isPlaying`/`m_isPaused = false`, XAudio2 flush) without calling `Stop()` or
  touching `m_threadManager`. The thread-safe `Stop()` path remains intact for all normal callers.

- *See: [`Configuration.cpp`](Configuration.cpp), [`ShaderLoaders.cpp`](ShaderLoaders.cpp),
  [`MoviePlayer.cpp`](MoviePlayer.cpp)*

**May 19, 2026** - SceneManager cache, models disk cache, version overlay, window client-area fix, renderer selection centralised (v0.0.1262):

**SceneManager — geometry pre-cache and bug fixes:**

- **Two-tier cache**: `models[MAX_MODELS]` survives scene switches; `scene_models[]` is per-scene. New `ModelInfo` fields (`sourceSceneFile`, `bGpuReady`, `cachedInstanceIndex`) drive a fast-path that skips all file I/O and GPU setup after first load.
- **Fast-path rebuild**: `ParseGLBScene`/`ParseGLTFScene` scan `models[]` for `bGpuReady` entries and rebuild `scene_models[]` from cache without file I/O, JSON parse, or `SetupModelForRendering`. Animations restarted via `ForceAnimationReset` + `StartAnimation`.
- **Write-back on first load**: both write-back blocks now call `models[modelSlot].CopyFrom(scene_models[instanceIndex])` so GPU resources (vertex/index buffers, SRVs, shaders) are live in both arrays simultaneously via ComPtr/shared_ptr AddRef.
- **FNV-1a vertex hasher**: replaced XOR-chained hash (collision-prone for repeated components) with FNV-1a over raw `VertexKey` bytes; `reserve()` hints added to eliminate rehash reallocations during vertex welding.
- **Single-pass slot scan**: one O(MAX_MODELS) pass tracking `modelSlot` + `firstFree`, replacing two sequential passes.
- **Load timing** (`_DEBUG_SCENEMANAGER_`): `high_resolution_clock` at BEGIN/END of both parse functions; fast-path hits log elapsed time.
- **Bug fix — cross-scene name collision / Z-flip contamination**: slot lookup now requires `name == primName` AND `(sourceSceneFile.empty() OR sourceSceneFile == m_currentSceneFile)`. Prevents scene B reusing scene A's slot when they share a model name.
- **Bug fix — transform-only node position cached as (0,0,0)**: position extracted from `worldTransform._41/_42/_43` instead of `m_modelInfo.position` (zeroed by `DestroyModel()` before read).
- **CACHE-WRITE / CACHE-RESTORE debug log lines**: confirm Z-flip preserved end-to-end in both `ParseGLB/GLTFNodeRecursive` write-back and fast-path restore.
- **`main.cpp` preprocessor fixes**: `||` inside `defined()` corrected; bare `#elif` replaced with `#else`.
- *See: [`SceneManager.h`](SceneManager.h), [`SceneManager.cpp`](SceneManager.cpp), [`Models.h`](Models.h), [`main.cpp`](main.cpp)*

**Models disk cache (`cache.dat`) — on-disk persistence of the global models[] pool:**

- **`MODELS_CACHE_FILENAME`** define added to `Includes.h`; resolves to `"cache.dat"` in the working directory.
- **`SceneManager::SaveCache(filepath)`**: serialises all loaded entries in `models[MAX_MODELS]` (CPU-side geometry, transforms, GLTF binary blob, material names, PBR properties) to a compact binary file on program exit — called in `main.cpp` before `fxManager.CleanUp()` / `scene.CleanUp()`.
- **`SceneManager::LoadCache(filepath)`**: called in `main.cpp` immediately after `scene.Initialize(renderer)`. Validates magic number, version, and `sizeof(Vertex)` before restoring each slot. If the cache file is absent a debug message (`_DEBUG_SCENEMANAGER_` only) informs the user that a full model reload is required.
- **Platform guards**: file-existence check uses `GetFileAttributesA()` on Windows, `access()` (POSIX) on Linux / Android / Apple / iOS; file I/O uses `std::fstream` throughout. Both methods compile cleanly on all five target platforms.
- **Debug output** (`_DEBUG_SCENEMANAGER_` + `_DEBUG` only): save reports model count written; load reports model count restored; absent-file case explicitly states "full model reload required".
- *See: [`Includes.h`](Includes.h), [`SceneManager.h`](SceneManager.h), [`SceneManager.cpp`](SceneManager.cpp), [`main.cpp`](main.cpp)*

**Version overlay — now shows `<GameName> <Platform> <Renderer> v<Major>.<Minor>.<Build>`:**

- **`GAME_NAME_W`**, **`PLATFORM_NAME_W`**, **`RENDERER_NAME_W`** compile-time wide-string macros added to `Includes.h` after the platform-detection block, covering Windows / Linux / Android / macOS / iOS and DirectX 11 / 12 / OpenGL / Vulkan.
- **`DXRenderFrame.cpp`** version overlay text updated from the hardcoded `"DirectX 11 v..."` to `GAME_NAME_W " " PLATFORM_NAME_W " " RENDERER_NAME_W " v..."` — adjacent string literals concatenated at compile time, zero runtime overhead.
- *See: [`Includes.h`](Includes.h), [`DXRenderFrame.cpp`](DXRenderFrame.cpp)*

**Window client-area resolution fix — renderer now gets exact configured dimensions:**

- **Root cause**: `CreateWindowEx` was passed the raw config resolution (e.g. 720×480) as OUTER window dimensions. Windows adds the title bar (~31 px) and thin DWM borders, leaving the client area ~30–40 px shorter than configured. `winMetrics.clientHeight` was therefore wrong at renderer initialization, causing `iOrigHeight` (used for all UI/camera math) to be off by the non-client overhead.
- **Fix** (`main.cpp`): Before `CreateWindowEx`, compute `wndOuterW/H` via `AdjustWindowRect(&wndRect, WS_OVERLAPPEDWINDOW, FALSE)` and pass those expanded values — the client area is now exactly 720×480 (or whatever `GameConfig.cfg` specifies). Config values are unchanged.
- This fix covers all three affected renderers (DX11, DX12, OpenGL) which all read `winMetrics.clientWidth/Height` at initialization; Vulkan was already correct (derives size from swap-chain surface extent).
- *See: [`main.cpp`](main.cpp)*

**Renderer selection centralised to Includes.h (MSVC 2022 project):**

- **Root cause**: `CrossPlatformGameEngine.vcxproj` was injecting the renderer preprocessor symbol (`__USE_DIRECTX_11__` etc.) via `$(_RendererDefine)` in `PreprocessorDefinitions`, derived from an `<ActiveRenderer>` UserMacro. This meant the active renderer was set in TWO places — the vcxproj AND the fallback block in `Includes.h` — and changing Includes.h alone had no effect.
- **`CrossPlatformGameEngine.vcxproj`**: removed `<ActiveRenderer>`, `<_RendererPrefix>`, and `<_RendererDefine>` property groups; stripped `$(_RendererDefine);` from both Debug and Release `PreprocessorDefinitions`; simplified `<TargetName>` from `$(_RendererPrefix)$(GameName)` to `$(GameName)` (output binary is now `CPGE.exe`). CMakeLists.txt is unchanged.
- **`Includes.h`**: the Windows renderer block is now an unconditional selection — uncomment exactly one `#define __USE_*__` to choose the compiled renderer. The old `#if !defined(...)` fallback guard is replaced by a `#error` that fires if none of the four symbols are defined, making misconfiguration a compile error rather than a silent wrong choice.
- *See: [`Includes.h`](Includes.h), [`CrossPlatformGameEngine.vcxproj`](CrossPlatformGameEngine.vcxproj)*

**Bug fix — C4005 macro redefinition warnings on CMake build (v0.0.1264):**

- **Root cause**: `CMakeLists.txt` was still passing `${RENDERER_DEFINE}` (e.g. `__USE_DIRECTX_11__`) via `target_compile_definitions`, meaning the symbol arrived TWICE — once on the compiler command line from CMake and once via `#define` in `Includes.h` line 83. This produced a C4005 warning for every translation unit in the CMake build path.
- **Fix — `CMakeLists.txt`**: removed `${RENDERER_DEFINE}` from `target_compile_definitions`. Renderer selection now lives exclusively in `Includes.h`; CMake still reads the `RENDERER` cache variable for output-name and status-message purposes but no longer injects the symbol at the compiler level.
- **Fix — `build/Debug/CrossPlatformGameEngine.vcxproj`**: stripped the stale `;__USE_DIRECTX_11__` entry from all four configuration `PreprocessorDefinitions` blocks in the currently generated file so VS builds are clean immediately. This file is regenerated on every `cmake-build.bat` run; the CMakeLists.txt change ensures future regenerations are also correct.
- *See: [`CMakeLists.txt`](CMakeLists.txt), [`build/Debug/CrossPlatformGameEngine.vcxproj`](build/Debug/CrossPlatformGameEngine.vcxproj)*

**May 21, 2026 — Cache-restore animation fix: ClearAllAnimations before re-parse, startup guard matches full-parse path (v0.0.1274–1276):**

- **Cache-restore animations not playing** (`v0.0.1274–1276`): After the v0.0.1273 cache-restore overhaul textures and materials restored correctly but animations never fired. Two defects fixed: (1) `gltfAnimator.ClearAllAnimations()` was not called before `ParseAnimationsFromGLTF` in either cache fast-path (GLB and GLTF). Without this, stale `m_animationInstances` from previous scene loads survived the re-parse, leaving the animator in an inconsistent state on every reload. (2) The cache fast-path's Step 5 did not check the return value of `CreateAnimationInstance` before calling `ForceAnimationReset` / `StartAnimation`, diverging from the full-parse startup path which guards those calls with `if (created)`. Both cache fast-paths now call `ClearAllAnimations()` before `ParseAnimationsFromGLTF` and mirror the full-parse startup exactly. Diagnostic logging added to Step 5 under `_DEBUG_SCENEMANAGER_` to trace `bAnimationsLoaded`, `GetAnimationCount()`, and per-animation `parentID` from `FindParentModelIDForAnimation`.
- *See: [`SceneManager.cpp`](SceneManager.cpp), [`BuildInfo.h`](BuildInfo.h)*

**May 20, 2026 — Shutdown fix, repeated-message guards, comprehensive cache-restore verification, texture reload on disk-cache restore, cache.dat write-once guard, unconditional cache revalidation, full cache-restore correctness overhaul all renderers (v0.0.1269–1273):**

- **Shutdown deadlock / continued rendering after fade**: `bIsShuttingDown` was being set *after* `RemoveWindow`, meaning the render thread could still compose and present frames during window teardown. Moved `threadManager.threadVars.bIsShuttingDown.store(true)` to immediately after the fade-wait completes (and before `RemoveWindow`). The render loop's existing early-exit guard (`if (bIsShuttingDown) return;`) now fires on the very next frame after the fade, so no frames are rendered during or after GUI teardown.
- **Repeated debug messages — `FindParentModelID`**: Warning `"Model not found in scene_models array"` was emitted every render frame when `SplashShipName` / `ShipName1` were temporarily absent from `scene_models[]` (common during scene transitions). Replaced the unconditional `logDebugMessage` with a `static std::unordered_set<std::wstring>` once-guard so each unique model name produces at most one warning per session.
- **Repeated debug message — `UpdateSceneAnimations` timer**: A 5-second periodic debug log `"Updating scene animations with deltaTime"` provided no diagnostic value and cluttered the trace. Removed entirely.
- **Cache restore — comprehensive GPU/material/texture verification** (`ParseGLBScene` and `ParseGLTFScene` both paths): The previous `if (!constantBuffer)` guard only triggered a GPU rebuild when the constant buffer was null. Replaced with a full verification block that independently checks and restores: (a) vertex/index geometry, (b) all three core GPU buffers (`constantBuffer`, `vertexBuffer`, `indexBuffer`), (c) diffuse `textureSRVs` from base `models[]` before falling back to a generated placeholder, (d) `normalMapSRVs`, (e) PBR channel SRVs (`metallicMapSRV`, `roughnessMapSRV`, `aoMapSRV`, `environmentMapSRV`), (f) CPU `m_materials` map, (g) `m_modelInfo.materials` name list. GPU rebuild + write-back to `models[]` only fires if a core buffer was absent; texture/material fields are restored in-place without requiring a full rebuild when the base cache already holds valid handles.
- **Root cause — PBR material properties lost on earlier cache restore**: `Model::CopyFrom` was copying `m_modelInfo` but silently skipping `m_materials`. Fixed by adding `m_materials = other.m_materials` in `CopyFrom` (earlier session).
- **Log spam — `CleanUp()` per-model Reset lines** (earlier session): Replaced per-model debug lines with a single post-loop summary.
- **Log spam — `LoadGLTFMeshPrimitives` primitive-count header** (earlier session): Guarded by `if (primitiveFilter == 0)` so it fires once per mesh.
- **Duplicate GLB header validation log** (earlier session): Removed redundant second log block.
- **Texture reload on disk-cache restore** (`v0.0.1270`): After a disk-cache restore, COM `textureSRVs` cannot be deserialised — `models[]` holds empty or fallback-placeholder SRVs. The previous verification block detected empty SRVs and called `SetupModelForRendering` → `LoadFallbackTexture`, which wrote bricks.png fallback SRVs back to `models[]`. On the next same-session reload `CopyFrom` copied the fallback SRVs (non-empty, so the check didn't re-fire), locking in the wrong texture permanently. Fixed in both `ParseGLBScene` and `ParseGLTFScene` mini-parse blocks: (a) before the mini-parse a `texturesNeedReload` flag is set if any loaded geometry model has an empty `textureSRVs` vector; (b) the mini-parse condition is widened to `(!bAnimationsLoaded || texturesNeedReload)`; (c) inside the `try` block, after `ParseMaterialsFromGLTF` and `ParseAnimationsFromGLTF`, a loop matches each affected `scene_models[i]` to its GLTF material by name, clears any fallback SRVs (including `metallicMapSRV`, `roughnessMapSRV`, `aoMapSRV`), calls `BindGLTFMaterialTexturesToModel` to bind real GPU textures from the GLB/GLTF binary, then writes all restored SRV and texture handles back to the matching `models[]` entry so subsequent same-session reloads copy real data.
- **`SaveCache` write-once guard** (`v0.0.1271`): `SceneManager::SaveCache` was unconditionally overwriting `cache.dat` on every clean exit. Added a `std::filesystem::exists(filepath)` check immediately after the empty-model guard; if the file is already present on disk the write is skipped and the function returns `true`. The existing cache from the previous session is preserved unchanged.
- **Cache restore — unconditional texture/material/animation revalidation** (`v0.0.1272`): The `ParseGLBScene` cache fast-path previously gated its mini-parse on two flags: `texturesNeedReload` (only true when `textureSRVs` was completely empty) and `!bAnimationsLoaded` (permanently false after the first session parse, never reset). This caused three silent failures: (1) stale-but-non-null SRVs — e.g. after a device reset or cross-scene reload — passed the empty check and were never rebound; (2) `BindGLTFMaterialTexturesToModel` was never called on same-session reloads with non-empty SRVs, so PBR material data was not re-applied; (3) if a different GLB was loaded between reloads, `gltfAnimator` held that scene's keyframes — the cache path skipped `ParseAnimationsFromGLTF` for the original file because `bAnimationsLoaded` was still `true`, producing wrong or absent animation. Fixed by removing the `(!bAnimationsLoaded || texturesNeedReload)` gate entirely (the GLB JSON + BIN is always re-read on every cache restore — cheap compared to geometry parsing), removing the inner `if (!textureSRVs.empty()) continue` guard so all geometry models are rebound, and updating the stale comment that still referenced "SRV vector is still empty".
- **Cache-restore correctness overhaul — all renderers** (`v0.0.1273`): The GLB and GLTF cache fast-paths had three structural defects that caused wrong geometry flip, broken animations, and missing textures on every reload: (1) The GLB file was re-read AFTER models were restored from cache, meaning `m_blenderConfig` (axis flip/winding), exporter detection, camera, and lights were all stale when models were written into `scene_models[]`. Fixed by restructuring both paths to parse the document FIRST — `DetectGLTFExporter`, `BlenderImports::BuildConfig`, `ParseGLTFCamera`, `ParseGLTFLights`, `ParseMaterialsFromGLTF`, and `ParseAnimationsFromGLTF` are all called before any `scene_models[]` entry is touched, exactly matching the first-load order. (2) GPU rebuild (vertex/index/constant buffer null-check → `SetupModelForRendering`) and texture write-back to `models[]` were wrapped in `#if defined(__USE_DIRECTX_11__)`, leaving Vulkan and OpenGL completely unguarded — a Vulkan reload never rebuilt missing `VkBuffer`/`VkPipeline` handles and never rebound descriptor sets; an OpenGL reload never checked `VAO`/`VBO`/`EBO`. Expanded to renderer-specific `#if` chains: DX11/DX12 check ComPtr nullness, Vulkan checks `VK_NULL_HANDLE`, OpenGL checks zero handles; write-back mirrors each renderer's GPU fields. (3) The silent `catch (...)` that swallowed all JSON parse exceptions is replaced with a logged `LOG_ERROR` + fallthrough to the full parse so errors are visible and the scene still loads correctly. The GLTF path had an additional bug: it only ran the mini-parse when `!bAnimationsLoaded || texturesNeedReload` — permanently skipping on same-session reloads. Both paths now: parse document first, fail safely to full parse if JSON is corrupt, and always rebind materials/textures for all renderers unconditionally.
- *See: [`SceneManager.cpp`](SceneManager.cpp), [`BuildInfo.h`](BuildInfo.h)*

---

**Bug fix — duplicate log output + materials/animations not restored after disk-cache load (v0.0.1265):**

- **Duplicate log output — `Debug.cpp`**: `logDebugMessage` was calling `logLevelMessage` (which already writes the tagged message to the log file) and then calling `Insert_Into_Log_File` a second time with the untagged message. Every debug call produced two log file lines — one with `[INFO]:` prefix and one without. Removed the redundant `Insert_Into_Log_File` call from `logDebugMessage`; `logLevelMessage` is the single authoritative writer.
- **Root cause of repeated parse messages**: `LoadGLTFMeshPrimitives` debug messages (`Processing N primitives`, `PRE-ALLOCATED N texture slots`, `gltfBinaryData size`) are inside `_DEBUG_SCENEMANAGER_` guards and are only called during a full parse. With the duplicate output fixed, each message appears exactly once per full parse. On subsequent reloads the fast-path returns `true` before the full parse is reached, so these messages do not repeat.
- **Materials not restored after disk-cache load**: `ParseMaterialsFromGLTF` is only called in the full-parse path; after a disk-cache startup the fast-path fired without it. Fixed in both `ParseGLBScene` and `ParseGLTFScene` fast-paths: if `!bAnimationsLoaded` (indicator that the session is a fresh startup from disk cache), the GLB/GLTF file is re-opened and its JSON + BIN chunks are read — no geometry processing — and `ParseMaterialsFromGLTF(miniDoc)` is called to restore material data.
- **Animations not restored after disk-cache load**: Same root cause. `gltfAnimator.ParseAnimationsFromGLTF` was never called in the fast-path on disk-cache startup, leaving `bAnimationsLoaded = false` and `gltfAnimator` empty. The same mini-parse now calls `gltfAnimator.ParseAnimationsFromGLTF(miniDoc, gltfBinaryData)`. Animation instances are then created via `CreateAnimationInstance` (a no-op if the instance already exists on same-session reloads) followed by `ForceAnimationReset` + `StartAnimation`.
- *See: [`Debug.cpp`](Debug.cpp), [`SceneManager.cpp`](SceneManager.cpp)*

**Resolution — all paths driven by GameConfig.cfg (fallback 800×600 if file absent):**

- **`CreateWindowEx`** passes `config.myConfig.resolutionWidth/Height` instead of `DEFAULT_WINDOW_WIDTH/HEIGHT`.
- **DX11 / DX12 `SetFullExclusive`**: `targetMode.RefreshRate.Numerator` reads `config.myConfig.refreshRate` (was hardcoded 60); windowed-restore fallback uses config values instead of `DEFAULT_WINDOW_WIDTH/HEIGHT`.
- **Vulkan `SetWindowedScreen`**: `SetWindowPos` and `Resize` use `config.myConfig.resolutionWidth/Height`.
- **DX11 / DX12 exclusive fullscreen — desktop capture and restore**: `EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &m_originalDesktopMode)` called before `SetFullscreenState(TRUE)`; new `bool m_isExclusiveFullscreen` member tracks state. `Cleanup()` calls `SetFullscreenState(FALSE, nullptr)` + `ChangeDisplaySettingsEx` with the saved `DEVMODE` before releasing the swap chain — previously the swap chain was freed while exclusive, leaving the desktop stuck at the game resolution until reboot.
- *See: [`main.cpp`](main.cpp), [`DX11Renderer.h`](DX11Renderer.h), [`DX11Renderer.cpp`](DX11Renderer.cpp), [`DX12Renderer.h`](DX12Renderer.h), [`DX12Renderer.cpp`](DX12Renderer.cpp), [`VULKAN_Renderer.cpp`](VULKAN_Renderer.cpp)*

**FXManager — fader viewport fix:**

- **DX11**: `RenderFullScreenQuad` calls `RSSetViewports(renderer->iOrigWidth × iOrigHeight)` before `Draw(4,0)`. D2D `EndDraw()` leaves D3D11 viewport undefined; inheriting it caused the fade to cover only the stale 800×600 region.
- **Vulkan**: `RenderFadeFullScreenQuad` calls `vkCmdSetViewport` + `vkCmdSetScissor` before `vkCmdDraw`. The pipeline uses `VK_DYNAMIC_STATE_VIEWPORT/SCISSOR` making these mandatory — previously the draw fired with no viewport.
- *See: [`DX_FXManager.cpp`](DX_FXManager.cpp), [`VULKAN_FXManager.cpp`](VULKAN_FXManager.cpp)*

**Bug fix — SceneManager disk-cache fast-path: "Model not loaded or constant buffer is invalid" (v0.0.1263):**

- **Root cause**: `LoadCache()` restored `bGpuReady = true` from disk even though GPU COM objects (constantBuffer, vertexBuffer, indexBuffer, samplerState, shaders) cannot be serialised. On the next `ParseGLBScene`/`ParseGLTFScene` call the fast-path fired, called `CopyFrom(models[m])` which shallow-copied null COM pointers, then set `m_isLoaded = true`. The render loop then called `Render()` → `UpdateConstantBuffer()` and failed the `!m_isLoaded || !m_modelInfo.constantBuffer` guard every frame, producing the error and D3D11 index-buffer / sampler warnings.
- **Fix 1 — `LoadCache()` — GPU re-serialisation**: after all data for a model is read from disk, `SetupModelForRendering()` is called to rebuild the GPU objects (constantBuffer, vertexBuffer, indexBuffer, lightConstantBuffer, materialBuffer, debugConstantBuffer, samplerState) from the cached geometry. `bGpuReady` starts `false` and is set `true` only if setup succeeds — if it fails the model safely falls through to a full GLB parse. Texture SRVs are not cached; fallback solid-colour textures are applied here and replaced by the actual asset textures when the first full parse writes back via `CopyFrom`. `bGpuReady = false` is always the initial state regardless of the serialised flag value.
- **Fix 2 — defensive guard in both fast-paths** (`ParseGLBScene` and `ParseGLTFScene`): after `CopyFrom(models[m])` inside the `bGpuReady` branch, a `#if defined(__USE_DIRECTX_11__)` guard checks `!constantBuffer && !vertices.empty()`. If true (GPU resources absent despite `bGpuReady`), `SetupModelForRendering(idx)` is called immediately and all resulting GPU handles are written back into `models[m]` so subsequent same-session reloads find valid resources in the fast-path.
- *See: [`SceneManager.cpp`](SceneManager.cpp)*

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
