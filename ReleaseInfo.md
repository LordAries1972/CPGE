# DirectX 11 & 12, OpenGL & Vulkan Game Engine

**Cross Platform Gaming Engine by Daniel J. Hobson**  
*Melbourne, Australia 2023-2026*

*Current Build Version: v0.0.1011*

---

## Table of Contents

### [License Information](#license-information)
### [Project Overview](#project-overview)
### [Release Status](#release-status)

### Development History by Month and Year

#### 2026

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
- OpenGL Renderer for Windows/Linux/MacOS/Android
- Vulkan Renderer for Windows/Linux/Android

**March 16, 2025** - Platform integrations:
- Steam Class implementation
- GooglePlay interface for Android
- AppStore interface for iOS

**May 25, 2025** - Optimization:
- Code optimizations and refactoring

**June 7, 2025** - Sub-Systems Extending:
- Some System Classes need more work to suit other platforms.
---

## Technical Requirements

- **Operating System**: Windows 10 SP1+ (64-bit)
- **Development Environment**: Visual Studio 2019/2022
- **Language Standard**: C++17 Compliant
- **Graphics APIs**: DirectX 11/12, OpenGL, Vulkan with a minimum or equivilant NVIDIA GTX-960M card.
- **Architecture**: Win x64, Linux, Android, iOS and MacOS only!

---

*End of File*
