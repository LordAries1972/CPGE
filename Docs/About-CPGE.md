# About CPGE — Cross Platform Gaming Engine

**Author:** Daniel J. Hobson — Melbourne, Australia  
**Current Build:** v0.0.1682  
**Language:** C++17  
**License:** Free-to-Use (Attribution Required) — See [Licensing](#37-licensing)

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [About the Author](#2-about-the-author)
3. [Project Philosophy & Design Principles](#3-project-philosophy--design-principles)
4. [Platform & Operating System Support](#4-platform--operating-system-support)
5. [Build System & Compilation](#5-build-system--compilation)
   - [5.1 Prerequisites](#51-prerequisites)
   - [5.2 Directory Layout](#52-directory-layout)
   - [5.3 cmake-build.bat — The Primary Build Script](#53-cmake-buildbat--the-primary-build-script)
   - [5.4 Renderer Selection — How It Works End to End](#54-renderer-selection--how-it-works-end-to-end)
   - [5.5 Debug vs Release Configurations — Compiler Flags in Detail](#55-debug-vs-release-configurations--compiler-flags-in-detail)
   - [5.6 CMakeLists.txt — Structure and Role](#56-cmakeliststxt--structure-and-role)
   - [5.7 Debug vs Release — Behavioural Differences at Runtime](#57-debug-vs-release--behavioural-differences-at-runtime)
   - [5.8 Visual Studio IDE Builds](#58-visual-studio-ide-builds)
   - [5.9 HLSL Shader Pre-Compilation](#59-hlsl-shader-pre-compilation)
   - [5.10 Output Executables](#510-output-executables)
   - [5.11 Adding New Source Files](#511-adding-new-source-files)
   - [5.12 Build Troubleshooting](#512-build-troubleshooting)
6. [Renderer Architecture — Multi-Backend Overview](#6-renderer-architecture--multi-backend-overview)
   - [6.1 The Renderer Abstract Base Class](#61-the-renderer-abstract-base-class)
   - [6.2 Shared Slot Layout](#62-shared-slot-layout)
   - [6.3 DirectX 11 Renderer](#63-directx-11-renderer)
   - [6.4 DirectX 12 Renderer](#64-directx-12-renderer)
   - [6.5 OpenGL Renderer](#65-opengl-renderer)
   - [6.6 Vulkan Renderer](#66-vulkan-renderer)
   - [6.7 RendererFactory — Instantiation and Hardware Checking](#67-rendererfactory--instantiation-and-hardware-checking)
7. [Shader Management System](#7-shader-management-system)
   - [7.1 Why a Dedicated Shader Manager Is Needed](#71-why-a-dedicated-shader-manager-is-needed)
   - [7.2 Core Data Structures](#72-core-data-structures)
   - [7.3 Shader Loading and Compilation Pipeline](#73-shader-loading-and-compilation-pipeline)
   - [7.4 Shader Program Linking](#74-shader-program-linking)
   - [7.5 Error Handling — Colour-Coded Fallback Shader](#75-error-handling--colour-coded-fallback-shader)
   - [7.6 Hot-Reloading (Debug Builds)](#76-hot-reloading-debug-builds)
   - [7.7 Thread Safety](#77-thread-safety)
   - [7.8 Integration with Engine Subsystems](#78-integration-with-engine-subsystems)
   - [7.9 Shader File Naming Convention](#79-shader-file-naming-convention)
   - [7.10 Statistics and Diagnostics](#710-statistics-and-diagnostics)
8. [Camera System](#8-camera-system)
9. [Scene Management System](#9-scene-management-system)
10. [3D Model System](#10-3d-model-system)
11. [GLTF Animation System](#11-gltf-animation-system)
12. [Lighting System](#12-lighting-system)
13. [Material & Texture System (PBR)](#13-material--texture-system-pbr)
14. [Visual FX Manager](#14-visual-fx-manager)
15. [Threading System](#15-threading-system)
16. [IO Loader Thread](#16-io-loader-thread)
17. [Sound Effects Manager (SoundManager)](#17-sound-effects-manager-soundmanager)
18. [Music Playback — XMMODPlayer](#18-music-playback--xmmodplayer)
19. [Media Player — WinMediaPlayer](#19-media-player--winmediaplayer)
20. [Text-to-Speech Manager (TTSManager)](#20-text-to-speech-manager-ttsmanager)
21. [Input Systems](#21-input-systems)
    - [21.1 Keyboard Handler](#211-keyboard-handler)
    - [21.2 Joystick & GamePad System](#212-joystick--gamepad-system)
22. [Physics Engine](#22-physics-engine)
23. [Mathematics Precalculation System](#23-mathematics-precalculation-system)
24. [Networking Manager](#24-networking-manager)
25. [Gaming AI System](#25-gaming-ai-system)
26. [Script Manager](#26-script-manager)
27. [File I/O System (FileIO)](#27-file-io-system-fileio)
28. [Configuration System (GameConfig.cfg)](#28-configuration-system-gameconfigcfg)
29. [Exception Handler & Debug System](#29-exception-handler--debug-system)
30. [Console Window](#30-console-window)
31. [GUI System (GUIManager)](#31-gui-system-guimanager)
32. [Screen Recorder](#32-screen-recorder)
33. [Compression System (PUNPack)](#33-compression-system-punpack)
34. [Randomiser Utility (MyRandomizer)](#34-randomiser-utility-myrandomizer)
35. [Blender Add-Ons](#35-blender-add-ons)
36. [Steam Integration (Future)](#36-steam-integration-future)
37. [Licensing](#37-licensing)
38. [Contact & Contributions](#38-contact--contributions)

---

## 1. Introduction

The **Cross Platform Gaming Engine (CPGE)** is a real-time, modular, high-performance game and multimedia engine written entirely in **C++17**, authored from the ground up by **Daniel J. Hobson** of Melbourne, Australia. Every line of engine code — from the graphics backends and audio mixers to the physics solver and compression routines — has been designed, written, and maintained entirely in-house, with no external wrapper libraries whatsoever. The sole exceptions are the platform store SDKs for Steam, Google Play, and the iOS App Store (mandatory for commercial distribution, not for development), and the MIT-licensed `nlohmann/json` parser used exclusively for configuration file I/O.

---

### 1.1 What Is CPGE?

At its core, CPGE is a complete, self-contained software foundation for building 2D and 3D games and interactive applications. It provides every system a developer needs to go from a blank project to a running, interactive game — without requiring any external game engine, middleware, or third-party framework.

Unlike commercial engines that abstract away the underlying APIs behind a proprietary editor and runtime, CPGE gives developers **direct, transparent access** to every subsystem. There are no hidden layers, no opaque scripting VMs sitting between your code and the hardware, and no licensing fees tied to your game's revenue. You work with clearly documented C++ classes that expose exactly what they do and why.

CPGE is not a visual editor or a drag-and-drop tool — it is a **programmer's engine**. Scenes are defined by GLTF 2.0 files authored in Blender (or any compatible DCC tool), game logic is written in C++17 (or optionally driven by `.cgs` scene scripts), and all configuration is managed through a human-readable JSON file (`GameConfig.cfg`). This approach keeps the engine transparent, version-control-friendly, and free of proprietary binary formats.

---

### 1.2 Multi-Backend Rendering Architecture

CPGE targets the complete spectrum of modern graphics APIs in a **single unified codebase**. The same engine source compiles and runs against four separate rendering backends:

| Backend | API | Primary Platform |
| --- | --- | --- |
| DirectX 11 | Direct3D 11, Direct2D 1.1, DirectWrite | Windows |
| DirectX 12 | Direct3D 12, 11on12, Direct2D 1.3 | Windows |
| OpenGL | OpenGL 4.x via GLEW | Windows, Linux, macOS |
| Vulkan | Vulkan 1.x, shaderc | Windows, Linux, Android |

This is achieved through a **conditional-compilation strategy** rather than runtime virtual dispatch in hot paths. When you build for DirectX 11, the compiler includes only the DX11 source code and links only the DX11 libraries — OpenGL, Vulkan, and DX12 code is not present in that binary at all. This means each backend binary is as lean and optimal as if it had been written exclusively for that API, while still sharing the full body of game-logic, audio, physics, networking, and scripting code.

The renderer selection is made once, at build time, by passing a flag to `cmake-build.bat` (see [Section 5](#5-build-system--compilation)). The result is four separate executables — one per backend — any of which can run the same game content without modification to the game's code or assets.

---

### 1.3 What CPGE Provides Out of the Box

A developer starting a new project with CPGE immediately has access to all of the following, fully integrated and ready to use:

#### Rendering & Graphics

- Four complete rendering backends (DX11, DX12, OpenGL, Vulkan) with identical game-facing APIs.
- Physically Based Rendering (PBR) material system with metallic-roughness workflow, 9 texture slots (diffuse, normal, metallic, roughness, AO, environment, gloss, emissive, shadow).
- Real-time shadow mapping with PCF (Percentage Closer Filtering) soft shadows.
- Directional, point, and spot lights with animation (flicker, pulse, strobe).
- 2D sprite rendering with a priority-ordered blit queue — for HUD, menus, and overlays.
- High-quality text rendering via DirectWrite on Windows (and a CPU fallback on other platforms).
- Full-screen visual effects (fades, particles, starfields, warp tunnels, text scrollers).

#### Asset Pipeline

- In-house GLTF 2.0 parser for `.gltf` and `.glb` scene files — no external importer needed.
- In-house FBX importer for assets from Maya, 3ds Max, and similar tools.
- Wavefront OBJ / MTL support for simpler geometry.
- Binary geometry cache (`cache.dat`) that eliminates re-parsing on subsequent runs.
- Blender add-ons that enforce correct export settings for the engine.

#### Audio

- DirectSound-based SFX system with priority queuing, cooldowns, fade-in, and stereo balance.
- Full XM (Extended Module / Fast Tracker 2) music player — handles pattern-based sequencing, 32 channels, and all standard XM effects entirely in software.
- MP3 / M4A streaming via Windows Media Foundation.
- In-game video playback (H.264 MP4 cutscenes) decoded to a fullscreen texture.
- Text-to-speech via the OS TTS engine for accessibility and developer diagnostics.

#### Game Logic

- Physics engine: rigid body motion, collision detection (AABB, sphere, continuous), ragdoll, projectiles, particle physics, audio Doppler.
- Networking: TCP/UDP dual-protocol, session auth, extensible command dispatch, checksum-validated packets.
- AI: real-time player behaviour analysis and adaptive enemy strategy generation.
- Scene scripting: `.cgs` text scripts with variables, loops, labels, jumps, and C++ function callbacks.
- Input: keyboard handler (hotkeys, combinations, AI logging) and joystick / gamepad (2D/3D modes, deadzone, remappable buttons).

#### Infrastructure

- Thread manager with named threads, RAII locks, graceful shutdown sequencing.
- Asynchronous asset loader thread (keeps the render thread at 60 fps during loads).
- Asynchronous file I/O with priority queue and PUNPack decompression integration.
- In-house lossless compression (PUNPack) for assets and network packets.
- Anti-tamper checksummed configuration file (JSON).
- Exception handler that writes annotated call-stack crash logs to disk on any fault.
- In-game developer console for live log viewing and command execution.
- Built-in screen recorder to MP4 (H.264 video + AAC audio via WASAPI loopback).
- Full GUI system (windows, buttons, sliders, tabs, input fields) rendered via Direct2D.

---

### 1.4 How the Engine Starts Up

Understanding the engine's startup sequence helps developers know where to insert their own initialisation code and why the order matters.

When the executable launches, the following sequence occurs:

1. **WinMain / main entry point** (`main.cpp`) — creates the OS window, initialises COM (`CoInitializeEx`), and reads `GameConfig.cfg` (or creates it with defaults if missing). The checksum is verified; a failed checksum resets configuration to defaults before proceeding.

2. **RendererFactory** instantiates the correct renderer class based on `rendererType` in the configuration. On Windows this is always one of `DX11Renderer`, `DX12Renderer`, `OpenGLRenderer`, or `VulkanRenderer` — whichever was compiled in.

3. **Renderer initialisation** — the renderer creates the device, swap chain, render-target view, depth-stencil view, the Direct2D/DirectWrite overlay layer, and all fixed-pipeline state objects (blend states, depth-stencil states, rasteriser states, samplers, root signatures/descriptor heaps on DX12). Shader files are loaded and compiled by the `ShaderManager` as part of this step.

4. **Subsystem initialisation** (in dependency order):
   - `ThreadManager` — starts the named-thread registry and creates all mutex objects.
   - `ExceptionHandler` — installs the OS-level crash hook.
   - `SoundManager` — initialises DirectSound, pre-loads the WAV SFX library.
   - `XMMODPlayer` — opens the first XM file from the playlist and prepares the mixing buffer.
   - `GUIManager` — loads all 2D UI textures, constructs the default GUI layout.
   - `FXManager` — initialises the effects queue and compiles FX shaders.
   - `LightsManager` — creates the GPU light constant buffer.
   - `SceneManager` — checks for `cache.dat`; if present, restores geometry from cache; if absent, marks that the first scene parse will write a fresh cache.
   - `NetworkManager` (if `__USE_NETWORKING__` defined) — binds sockets, starts the network receive thread.
   - `GamingAI` (if `__USE_GAMINGAI__` defined) — loads the persisted behaviour model (if any) and starts the AI analysis thread.
   - `ScriptManager` (if `__USE_SCRIPT_MANAGER__` defined) — prepares the script parser and registers built-in C++ engine functions.

5. **IO Loader Thread starts** — the `IOLoaderThread` begins running on `THREAD_LOADER`. It immediately enters the first scene's load sequence: parse the GLTF scene file (or restore from cache), upload all model geometry to GPU buffers, bind textures, configure lights, set up the default camera, start the intro music, and signal `bLoadComplete`.

6. **Render loop begins** — `main.cpp` enters the `WM_PAINT`-driven render loop. Each frame:
   - Delta time is computed from `std::chrono::high_resolution_clock`.
   - Input is polled (`KeyboardHandler`, `Joystick`).
   - Physics is updated (`Physics::Update(deltaTime)`).
   - AI state is updated (`GamingAI::Update(deltaTime)`) if enabled.
   - Script per-frame rules are evaluated (`ScriptManager::Update(deltaTime)`) if enabled.
   - FX effects are advanced (`FXManager::Update(deltaTime)`).
   - The renderer's `RenderFrame()` is called — this draws the 3D scene, overlays the FX layer, then the 2D Direct2D GUI layer, and finally calls `Present()` on the swap chain.

7. **Scene transitions** — when a scene change is requested, the IO Loader Thread is signalled. It waits for the current frame to finish (`bIsRendering` drain), then tears down the current scene's GPU resources, loads the next scene, and signals ready. The render loop seamlessly continues rendering whatever is loaded.

8. **Shutdown** — on `WM_CLOSE` / `WM_DESTROY`, all threads are signalled to stop via `ThreadManager` in reverse dependency order, GPU fences are drained, COM objects are released, and the updated `GameConfig.cfg` is written to disk before process exit.

---

### 1.5 How 2D and 3D Content Coexist

CPGE renders 2D and 3D content in distinct layers each frame, composited together without any render-to-texture round-trip:

1. **3D layer** — All 3D models, lights, and shadows are drawn first, writing to the depth buffer and the colour back buffer via the 3D pipeline (vertex/pixel shaders, constant buffers, texture samplers).

2. **FX layer** — Visual effects (particles, starfields, fade quads, warp tunnels) are drawn on top of the 3D content. Most FX effects use the same 3D device context / command list but draw either as screen-space quads or procedural geometry with no depth writes (so they always appear in front of 3D geometry).

3. **2D / GUI layer** — All GUI windows, buttons, text labels, HUD elements, the developer console, and 2D sprites are drawn last via the Direct2D / DirectWrite overlay. This layer is rendered directly onto the same DXGI swap-chain surface as the 3D content using a shared surface (`IDXGIBackBuffer` → `ID2D1Bitmap` interop on DX11; the 11on12 wrapped resource pattern on DX12 and Vulkan), so there is no expensive copy from a 2D render target to the 3D back buffer — they share the exact same memory.

This layered approach means a developer can mix rich 3D environments, full-screen visual transition effects, and crisp GUI text all in the same frame at 60 fps without any performance compromise.

---

### 1.6 Current Development Status

> **Build v0.0.1682 — June 2026**

| Backend | Rendering | FX | Models | Animation | GUI / 2D |
| --- | --- | --- | --- | --- | --- |
| DirectX 11 | Complete | Complete | Complete | Complete | Complete |
| DirectX 12 | Complete | Complete | Complete | Complete | Complete |
| OpenGL | Active development | Complete | Complete | Complete | Complete |
| Vulkan | Active development | Complete | Complete | Complete | Complete |

DirectX 11 is the **primary battle-tested backend** against which all features are first developed and verified. DirectX 12, OpenGL, and Vulkan are fully implementing parity with the DX11 path as the primary development focus for the current release cycle. All four backends compile and run; feature gaps are tracked and addressed systematically.

[Back to Table of Contents](#table-of-contents)

---

## 2. About the Author

| Field | Detail |
| --- | --- |
| **Name** | Daniel J. Hobson |
| **Location** | Melbourne, Victoria, Australia |
| **Titles** | Systems Analyst · Engine Architect · Software Developer · Business Systems Analyst |
| **Experience** | 35+ Years in Systems Engineering, Analysis, and Software/Systems Design & Development |
| **GitHub** | [https://github.com/LordAries1972/CPGE](https://github.com/LordAries1972/CPGE) |
| **Website** | [https://ultimanium.com/index.php?action=cpge](https://ultimanium.com/index.php?action=cpge) |

---

### 2.1 Professional Background

Daniel J. Hobson is a veteran **Systems Analyst, Engine Architect, and Software Developer** based in Melbourne, Victoria, Australia. With over 35 years of hands-on professional experience, his career has spanned an unusually wide range of computing domains — from large-scale enterprise business systems and embedded hardware interfaces, through to real-time multimedia pipelines, low-level graphics API programming, and full game engine architecture.

He began writing software at a time when programming was a discipline that demanded a thorough understanding of the hardware beneath you. There were no visual editors that abstracted away memory management, no garbage collectors to clean up after poor allocation decisions, and no framework layers that shielded you from the consequences of bad pointer arithmetic. This formative environment produced a developer who thinks in terms of **what the hardware is actually doing** — how data moves through CPU caches, how the GPU command queue drains, how OS scheduling affects real-time loop timing — rather than in terms of what a framework is supposed to do for you.

This background in low-level systems thinking is directly visible throughout CPGE's architecture. Buffer management decisions, threading strategies, constant-buffer layout rules, and the choice to implement audio mixing in software rather than delegating it to a third-party library all reflect an engineer who is most comfortable working as close to the metal as the problem allows.

---

### 2.2 What He Built Before CPGE

Before CPGE, Daniel's professional work included:

- **Enterprise Business Systems** — large-scale business analysis, systems design, and software development for enterprise environments. This background gives CPGE an unusually rigorous approach to data integrity, configuration validation, and fault tolerance that is often absent in purely game-focused engines.

- **Embedded Systems & Hardware Integration** — real-time work with hardware requiring deterministic timing, tight resource budgets, and zero-tolerance failure modes. This is the origin of CPGE's strict performance targets (60 fps is a hard requirement, not a guideline), its minimal-allocation philosophy in hot paths, and its preference for fixed-size buffers over dynamic containers where timing predictability matters.

- **Multimedia Pipeline Development** — extensive work with audio and video processing pipelines, including codec integration, buffer management for streaming media, and real-time mixing. This directly produced CPGE's in-house XM tracker player, its DirectSound SFX manager, and its Windows Media Foundation integration — all built from a deep understanding of how audio data flows from a file on disk to the speaker without artefacts.

- **Cross-Platform Compatibility Analysis** — analysis and design work for systems required to run across multiple operating systems and hardware targets. This is the direct origin of CPGE's platform-abstraction architecture: the conditional-compilation strategy, the `PLATFORM_WINDOWS` / `PLATFORM_LINUX` / `PLATFORM_ANDROID` / `PLATFORM_APPLE` / `PLATFORM_IOS` guards, and the goal of a single codebase that produces correct, efficient binaries for every target.

---

### 2.3 Why He Built CPGE

CPGE was not created to fill a gap in the commercial market — it was created out of genuine passion for the craft of engine programming, a desire to preserve and demonstrate old-school systems programming techniques that are increasingly rare in the modern development landscape, and a strong personal conviction about the cost of games to consumers.

Daniel's driving motivations are:

**Preservation of Low-Level Programming Knowledge**
There is a growing concern in the software industry that the generation of developers entering the workforce today has limited exposure to low-level systems programming. High-level frameworks, visual scripting, and abstraction-heavy engines have made it possible to ship a product without ever understanding what the GPU is doing, why a heap allocation in a render loop is problematic, or how a mutex prevents a race condition. CPGE exists, in part, as a living demonstration that these skills still matter — and as a reference implementation that younger developers can study to learn them.

**Freedom from Commercial Engine Fees**
Major commercial game engines charge royalties or licence fees that pass directly into the cost of the final game — costs that are ultimately borne by the consumer. Daniel believes this cycle is unnecessary and harmful to the gaming community. If a professional-quality engine can be provided free of charge with no royalties, game developers can price their products lower while still making a fair return. CPGE is his practical answer to this: a fully capable engine with zero fees, zero royalties, and a single attribution requirement.

**Full Transparency and Control**
When you build on a third-party engine, you accept that there are parts of the system you cannot inspect, change, or diagnose. When the engine has a bug that affects your game, you wait for the engine vendor's fix. When the engine's performance characteristics do not match your game's needs, you work around them. CPGE removes this entirely — every subsystem is in the source, every decision is documented, and every developer using CPGE has the same complete access to the codebase as the author.

**A Platform for His Own Game**
CPGE is also the engine being used to build Daniel's own upcoming game, **"The Shadows of Orion"** — a 3D flight-based game that will serve as the engine's first full commercial demonstration. This means CPGE is not a theoretical exercise — every subsystem must work well enough to ship a real game on it, which drives a much higher standard of quality and completeness than a pure academic or hobbyist project.

---

### 2.4 His Engineering Principles

The principles that govern how Daniel writes code are not abstract ideals — they are observable throughout the CPGE codebase and have direct, practical consequences for developers who use the engine.

**Own what you ship.** If your product depends on it, you should understand it completely. External libraries are accepted only when the cost of building the equivalent exceeds the benefit by a clear margin (as with the platform store SDKs and the JSON parser). Everything else is built in-house, documented in-house, and debugged in-house.

**Diagnostics are not optional.** A crash with no information is worse than no crash — it destroys developer time. Every subsystem in CPGE emits structured log messages at multiple severity levels. The exception handler produces a human-readable call-stack log with breadcrumb trail on any fault. Shader compilation failures produce a colour-coded fallback rendering so the fault is immediately visible on screen rather than producing a silent black frame. These are not afterthoughts — they are designed-in features of every subsystem.

**Strict standards compliance is not pedantry — it is future-proofing.** Code that relies on undefined behaviour, compiler extensions, or platform-specific assumptions has a limited lifespan. CPGE targets C++17 strictly, and every API call is made against the documented specification for that API. This is why the engine compiles cleanly on multiple compilers and why porting to new platforms is a matter of adding the platform-specific renderer blocks rather than fixing widespread undefined behaviour.

**Performance is a design constraint, not an optimisation pass.** Decisions that would compromise the 60 fps target are rejected at the design stage. Hot paths avoid heap allocation, use lookup tables for expensive math, cache COM objects rather than recreating them per frame, and use atomics rather than mutexes for the lightest synchronisation operations. This approach is rooted in Daniel's embedded systems background, where performance requirements are non-negotiable constraints rather than aspirational targets.

**Readability and comments are mandatory.** The CPGE codebase is heavily commented at every level. Every non-obvious decision has a comment explaining why it was made. Every function that interacts with a platform API has comments referencing the specific API behaviour being relied upon. This is intentional — CPGE is also a learning resource, and code that cannot be understood is code that cannot be learned from.

---

### 2.5 His View on the Gaming Industry

Daniel has strong and plainly-stated views on the modern gaming industry, particularly around pricing and the economics of game engines. These views shaped the licence terms for CPGE and the entire motivation behind making it free.

He believes the era of "gotcha" mobile games, season passes for content that should have been in the base product, and premium engine royalties added to development costs have collectively made gaming unnecessarily expensive for consumers. His position is straightforward: if a professional engine is free to use, developers have lower production costs, and those lower costs can be passed on to players. Lower prices increase the size of the potential market, which benefits everyone in the chain.

CPGE is his direct, practical contribution toward that outcome. It is the engine that he wishes had existed when he started building games — professional, complete, transparent, and entirely free.

---

### 2.6 Current Focus

As of June 2026, Daniel's active work is split between:

- **Engine Development** — bringing DirectX 12, OpenGL, and Vulkan to full feature parity with the DX11 backend; completing the window-resize pipeline across all four backends; and advancing the scene management, FBX import, and animation systems.
- **Game Development** — pre-production design work on *The Shadows of Orion*, the first full game to be built and released on CPGE. This game will serve as the definitive public demonstration of the engine's capabilities.
- **Documentation** — this document and the broader suite of per-subsystem usage guides in the `Docs/` folder, ensuring that developers who want to use CPGE can get up to speed without needing to reverse-engineer the codebase.
- **Community Building** — the CPGE project website at [ultimanium.com](https://ultimanium.com/index.php?action=cpge) provides a forum and project news for contributors and interested developers.

For further background on the project's origins and a more personal statement from the author, see [ABOUT.md](ABOUT.md).

[Back to Table of Contents](#table-of-contents)

---

## 3. Project Philosophy & Design Principles

CPGE's development is governed by a set of non-negotiable engineering principles. These are not aspirational ideals written for a readme — they are **active constraints** that have shaped every subsystem, every API decision, and every architectural trade-off made in the codebase. Understanding them is essential for any developer who wants to extend the engine, contribute to it, or simply understand why it is built the way it is.

---

### 3.1 No Third-Party Wrappers

Every subsystem in CPGE is implemented from scratch, in-house, with no reliance on external libraries beyond the platform SDKs and one MIT-licensed JSON parser. This is a deliberate and carefully considered engineering position, not a reflexive rejection of all external code.

#### Why Third-Party Libraries Are Avoided

Third-party libraries introduce a class of problems that in-house implementations do not:

- **Hidden performance overhead.** Most general-purpose libraries are designed to handle a wide variety of use cases. They allocate memory defensively, copy data conservatively, and optimise for correctness across many scenarios rather than for the specific access patterns of a real-time game loop. When you use a library, you accept whatever allocation strategy it has chosen — and in a 16 ms frame budget, unexpected allocations in hot paths produce frame-time spikes that are extremely difficult to trace.

- **Uncontrollable dependency chains.** A library depends on other libraries, which depend on others. Each link in this chain is a potential source of breaking changes, licence renegotiations, or abandonment. CPGE has no dependency chain beyond the OS platform SDKs, which are stable by definition.

- **Black-box debugging.** When something goes wrong inside a third-party library, you are looking at a call stack that passes through code you cannot read, cannot modify, and may not even have debug symbols for. Every hour spent debugging a third-party library failure is an hour that could have been spent fixing the problem. CPGE's in-house subsystems are fully transparent — every function is readable, every decision is commented, and every failure path emits a diagnostic message.

- **Licence incompatibility risk.** Libraries change licences. What is permissive today may acquire a commercial requirement in the next major version. CPGE's zero-dependency policy eliminates this risk entirely for the engine core.

#### What Is Permitted

The principle is not absolute — it is applied with engineering judgement:

- **`nlohmann/json`** — The JSON parser by Niels Lohmann ([https://github.com/nlohmann/json](https://github.com/nlohmann/json)) is used for reading and writing `GameConfig.cfg`. The cost of building a correct, robust JSON parser from scratch is very high (JSON has many edge cases), the library is header-only and MIT-licensed (no build dependency, no licence risk), and configuration I/O is not a performance-critical path. This is a justified exception.

- **Platform Store SDKs** (Steam `steam_api.lib`, Google Play services, iOS App Store frameworks) — These are mandatory for distribution on their respective platforms. There is no in-house alternative. They are included in the repository (in the `steam/` folder for the Steam SDK) but are **excluded from all builds** until a commercial release is actively prepared. No game-logic code may call into these SDKs without explicit authorisation.

- **Vulkan SDK / shaderc** — The Vulkan SDK itself is a mandatory platform SDK for Vulkan development, analogous to the DirectX SDK. `libshaderc` (the runtime GLSL compiler within the Vulkan SDK) is used optionally for the FX manager's fullscreen quad shader — the engine degrades gracefully if shaderc is not present.

#### What This Means for Developers

When extending CPGE, the in-house rule applies to your contributions as well. If you need a new capability — a compression algorithm, a path-finding routine, a new file format parser — the expectation is that you implement it in C++ within the engine. If you believe a library exception is justified, document the case clearly (licence, performance analysis, cost-to-build estimate) and raise it for review on the project's GitHub.

---

### 3.2 Strict Conditional Compilation

CPGE supports four graphics backends in a single source tree. The mechanism that makes this feasible without performance penalty is **preprocessor-guarded conditional compilation** — one of the central structural decisions of the entire codebase.

#### How It Works

At build time, exactly one renderer token is defined for the entire compilation unit:

| Token | Meaning |
| --- | --- |
| `__USE_DIRECTX_11__` | Compile the DirectX 11 backend |
| `__USE_DIRECTX_12__` | Compile the DirectX 12 backend |
| `__USE_OPENGL__` | Compile the OpenGL backend |
| `__USE_VULKAN__` | Compile the Vulkan backend |

This token is injected by `cmake-build.bat` via CMake's `add_compile_definitions()` call. Every source file in the engine receives exactly one of these defines at compile time, and only one.

The master include file, [`Includes.h`](../Includes.h), uses this token to select which SDK headers to include and which libraries to link. For example, `__USE_DIRECTX_11__` causes `<d3d11.h>`, `<d3dcompiler.h>`, `<xaudio2.h>`, `<dsound.h>`, `<d2d1_1.h>`, and `<dwrite.h>` to be included, and links `d3d11.lib`, `dxgi.lib`, `xaudio2.lib`, `dsound.lib`, `d2d1.lib`, and `dwrite.lib`. None of the Vulkan, OpenGL, or DX12 headers or libraries are present in the DX11 binary at all.

#### Guards Throughout the Codebase

The guards propagate through every file that has renderer-specific behaviour. For example, a shared file like `Models.cpp` that handles model loading for all backends uses blocks such as:

```cpp
#if defined(__USE_DIRECTX_11__)
    // DX11-specific buffer creation via ID3D11Device
#elif defined(__USE_DIRECTX_12__)
    // DX12-specific upload heap / default heap copy
#elif defined(__USE_OPENGL__)
    // OpenGL VAO / VBO / EBO creation
#elif defined(__USE_VULKAN__)
    // Vulkan VkBuffer + VkDeviceMemory allocation
#endif
```

This means that when you are reading the DX11 binary's assembly output, there is genuinely no Vulkan or OpenGL code present — not dead code, not disabled code paths, but code that was never compiled into that translation unit.

#### Why Not Runtime Selection?

A common alternative is to use a `Renderer` abstract base class with virtual methods, and select the backend at startup based on configuration. CPGE does have a `Renderer` base class and `RendererFactory` for startup selection — but the heavy-cost work (buffer creation, shader compilation, resource binding) is not virtualised in hot paths.

Virtual dispatch in a render loop adds indirect call overhead on every draw call. More significantly, with runtime selection, **all backends must be present in the binary** — all their headers, all their data structures, all their memory layouts. This increases binary size, link time, and the number of SDK dependencies that every platform must have installed. It also means a bug in the Vulkan path could theoretically affect DX11 users through shared state, even if the Vulkan code path is never reached at runtime.

Conditional compilation eliminates all of this. The DX11 binary has no Vulkan code, no Vulkan headers, no Vulkan state, and no Vulkan bugs.

#### Guard Rules for Contributors

Every source file that calls a renderer-specific API **must** wrap that call in the correct `#if defined(...)` guard. This applies equally to new code added to existing shared files and to new source files added to the project. The build system enforces this — a shared file with an unguarded `ID3D11Device*` field will fail to compile on the OpenGL build with a clear error, surfacing the missing guard immediately.

The `logLevelMessage` function always takes exactly **two arguments** (a `LogLevel` and a `const wchar_t*` message) across all backends — this is a fixed interface that must not be altered.

---

### 3.3 Thread Safety by Design

CPGE runs multiple concurrent execution threads simultaneously during normal operation. Thread safety is not an afterthought applied to subsystems after they are written — it is designed in from the start, with each subsystem knowing exactly which thread it owns, which threads may call into it, and what protection is required for each shared resource.

#### The Named Thread Model

Every thread in CPGE is registered with the `ThreadManager` by a logical name from the `ThreadNameID` enum. This replaces raw OS thread IDs with meaningful identifiers that appear in log output, status queries, and shutdown sequencing:

| Thread Name | Owner Subsystem | Responsibility |
| --- | --- | --- |
| `THREAD_RENDERER` | Renderer | The primary render loop — draws frames and calls Present() |
| `THREAD_LOADER` | IOLoaderThread | Loads assets from disk, uploads to GPU, signals completion |
| `THREAD_AUDIO` | SoundManager / XMMODPlayer | Mixes audio channels and fills DirectSound buffers |
| `THREAD_NETWORK` | NetworkManager | Receives and sends network packets |
| `THREAD_AI` | GamingAI | Runs the player behaviour analysis pass |
| `THREAD_FILEIO` | FileIO | Drains the file I/O priority queue |

No two threads in this table own the same data. Data that must cross thread boundaries does so through one of three mechanisms:

1. **Atomic flags** (`std::atomic<bool>`, `std::atomic<int>`) — for simple state signals (e.g., `bLoadComplete`, `bIsRendering`, `bIsResizing`). Atomics are used when the shared value is a single word and only needs to be read or written, not compared-and-swapped on complex state.

2. **Mutex-protected critical sections** — for shared collections or multi-field structures that must be read or written atomically as a unit (e.g., the effects queue in `FXManager`, the model list in `SceneManager`). Mutexes are always named, always created through `ThreadManager`, and always acquired through `ThreadLockHelper`.

3. **Cross-thread command queues** — for operations that must execute on a specific thread (e.g., GPU buffer uploads must happen on the render thread's command list). The loader thread places the command in a queue; the render thread drains it at the start of the next frame.

#### ThreadLockHelper — RAII Lock Management

The `ThreadLockHelper` class is a RAII (Resource Acquisition Is Initialization) wrapper around a named mutex. Its purpose is to guarantee that a locked mutex is **always** released — even if the protected code block throws an exception, returns early, or is abandoned due to a logic error.

```cpp
// Correct usage — lock is acquired on construction:
{
    ThreadLockHelper lock(threadManager.GetLock("FXManager"));
    // Safe to read/write fxManager.effects here.
}
// Lock is automatically released here, whether we reach this
// line normally or via an exception unwinding the stack.
```

Without RAII guards, a missed unlock in an error path leaves the mutex permanently locked, causing the next thread to attempt acquisition to deadlock indefinitely — a fault that is extremely difficult to reproduce and diagnose. `ThreadLockHelper` makes this class of bug impossible.

`ThreadLockHelper` also supports:

- **Timeout-based acquisition** — if a lock cannot be acquired within a configurable timeout period, it logs a warning rather than blocking indefinitely, making potential deadlocks detectable in Debug builds.
- **Multiple-lock acquisition** — when a function must hold two or more locks simultaneously, `ThreadLockHelper` enforces a consistent acquisition order to prevent the classic A→B / B→A deadlock pattern.

#### The bIsRendering Drain Pattern

A specific thread-safety pattern used throughout CPGE is the `bIsRendering` atomic drain. Before any operation that would mutate GPU-visible resources that are currently in use by the render thread (e.g., removing an effect from the FX queue, destroying a model's vertex buffer, resizing the swap chain), the mutating thread must wait for `bIsRendering` to reach `false`.

```cpp
// Render thread sets this true at frame start, false at frame end.
// Mutating thread waits before touching GPU resources:
while (renderer->bIsRendering.load()) { /* spin or sleep */ }
// Now safe to release or modify the resource.
```

This guarantees the GPU is not mid-draw on the resource being released, preventing use-after-free crashes on GPU-side data.

---

### 3.4 60 fps Performance Target

**60 frames per second is a hard engineering requirement for every CPGE rendering backend**, not a guideline or a best-effort target. This means every design decision — from constant buffer layout to thread synchronisation strategy — must be evaluated against its frame-time impact.

A frame at 60 fps has a budget of approximately **16.67 milliseconds** from the start of the render call to the call to `Present()`. The engine must complete the following within that window:

- Input polling (keyboard, joystick).
- Per-frame script rule evaluation.
- Physics and AI state updates.
- FX effect advancement.
- Full 3D scene draw (model transforms, lighting, shadows).
- FX layer draw (particles, fades, starfields).
- 2D GUI and text draw via Direct2D.
- Swap chain `Present()`.

Every millisecond spent on one item is a millisecond unavailable to the others. The following specific techniques are applied across the engine to stay within budget:

#### Pre-Computed Lookup Tables

The `MathPrecalculation` system pre-computes sine, cosine, tangent, and their inverses at startup and stores them in fixed-size arrays. Every subsystem that needs trigonometric values in a per-frame loop (the physics update, the particle system, the FX warp tunnel, the camera arc calculation) uses the `FAST_SIN` / `FAST_COS` / `FAST_TAN` macros to retrieve pre-computed results rather than calling the CPU's floating-point sine/cosine instructions. This is particularly impactful in particle systems where hundreds of angle calculations happen per frame.

#### Zero Heap Allocation in Hot Paths

The render loop, the audio mixing callback, and the physics update loop do not allocate from the heap. All buffers, collections, and working memory used in these paths are pre-allocated at startup and reused each frame. The reason is that heap allocation (`new` / `malloc`) calls into the OS allocator, which may block on a lock shared with other threads. In a real-time loop this produces non-deterministic frame-time spikes — frames that usually cost 14 ms occasionally spike to 20 ms or more when the allocator contends.

Where dynamic-size collections are unavoidable (e.g., the FX effects queue), they use `std::vector` with `reserve()` called at startup to pre-allocate capacity, preventing reallocation during normal operation.

#### Cached COM Objects

On Windows, Direct2D and DirectWrite COM objects (text formats, solid-colour brushes, text layouts) are expensive to create — each creation involves heap allocation, reference counting, and in some cases round-trips to the GPU driver. CPGE caches these objects in `std::unordered_map<key, ComPtr<T>>` structures keyed by their parameters (font name, font size, weight, colour). A `GetOrCreateTextFormat()` pattern is used throughout all four renderer backends:

```cpp
// On first call with these params, the format is created and cached.
// On all subsequent calls, the cached pointer is returned immediately.
IDWriteTextFormat* fmt = GetOrCreateTextFormat(L"Arial", 14.0f, DWRITE_FONT_WEIGHT_NORMAL);
```

This eliminates the single largest source of per-frame COM allocation that was historically producing frame spikes and clean-exit crashes (where the allocator detected a COM object still being used on a thread that had been destroyed).

#### Atomic vs. Mutex Selection

The choice between a `std::atomic` and a `std::mutex` has a direct frame-time impact. An atomic load or store is a single CPU instruction on x86 (`MFENCE` / `LOCK XCHG`). A mutex acquisition involves a kernel transition if the mutex is contended, which can cost hundreds of microseconds.

CPGE uses atomics for all simple boolean and integer state that crosses thread boundaries (completion flags, resize flags, rendering flags), and reserves mutexes for multi-field data structures that require exclusive access during a sequence of operations.

#### Delta-Time Capping

The engine computes a per-frame `deltaTime` using `std::chrono::high_resolution_clock`. However, an uncapped delta time is dangerous — if the application is suspended by the OS, minimised, or experiences a one-time spike (e.g., the OS paging in a large asset), the next frame's delta time could be several seconds. This would cause physics objects to teleport, animations to skip, and audio sync to break.

CPGE caps the delta time to a configurable maximum (typically 100 ms — 6 frames) before passing it to physics, animation, and FX update calls. This means a system hiccup causes a momentary slowdown rather than a physics explosion.

#### DX12 Fence Strategy

DirectX 12 requires explicit CPU/GPU synchronisation via fences. An incorrectly implemented fence strategy can stall the CPU waiting for the GPU to finish the previous frame before recording the next one — effectively halving throughput. CPGE uses a **double-buffered command allocator** pattern: two command allocators are maintained, one for the frame currently being recorded by the CPU and one for the frame currently being processed by the GPU. The CPU only waits on the fence when both frames are in flight and it needs to reuse the older allocator — at 60 fps this wait is typically zero because the GPU has finished the previous frame by the time the CPU needs it again.

---

### 3.5 Debug-Centric Construction

A game engine that is difficult to diagnose is a game engine that is difficult to use. CPGE treats diagnostics as a first-class feature — not as logging sprinkled in as an afterthought, but as a structured, multi-level system that is deeply integrated with every subsystem.

#### The Debug Logging System

The `Debug` class provides a structured message logging interface used by every subsystem in the engine. Messages are emitted with an explicit severity level:

| Level | Macro / Constant | Meaning |
| --- | --- | --- |
| `LOG_INFO` | Informational | Normal operational status messages (scene loaded, thread started, audio playing). |
| `LOG_WARNING` | Warning | Unexpected but recoverable situations (texture not found — using fallback, lock timeout exceeded). |
| `LOG_ERROR` | Error | A subsystem operation failed but the engine can continue (vertex buffer creation failed, file not found). |
| `LOG_CRITICAL` | Critical | A fatal error — the engine cannot continue safely (renderer device lost, out of memory). |

All messages are written simultaneously to:

- The **in-game developer console** (see [Section 30](#30-console-window)), appearing in real time as the game runs.
- A **log file** on disk, persisting through crashes and across sessions.
- **Visual Studio's Output window** via `OutputDebugStringW` in `_DEBUG` builds, so log messages appear inline with the debugger.

The log includes a timestamp, the thread ID that emitted the message, and the severity level prefix, making it straightforward to correlate log entries with what was happening in which thread at the time.

#### The ExceptionHandler — Crash Reporting

The `ExceptionHandler` class installs low-level OS exception hooks at startup that catch **both C++ exceptions and platform-level hardware faults** (access violations, divide-by-zero, illegal instruction, stack overflow):

- **Windows** — `SetUnhandledExceptionFilter` (SEH — Structured Exception Handling) catches hardware faults and unhandled Win32 exceptions. `std::set_terminate` catches C++ exceptions that propagate out of `main`. Together they ensure no crash goes unlogged.
- **Linux / macOS** — `sigaction` handlers for `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`, and `SIGABRT`.
- **Android** — NDK signal handlers with `/proc/self/maps` for backtrace address resolution.

On any unhandled fault, the handler writes `Except-CallStack.log` to the process working directory. The log contains:

```text
=== CPGE Exception Report ===
Exception Type  : Access Violation (0xC0000005)
Fault Address   : 0x00007FF6A3B21F44
Process ID      : 14832
Thread ID       : 9216
Platform        : Windows 11 x64
Timestamp       : 2026-06-14 15:42:31

=== Call Stack (25 frames) ===
[00] CPGE!Models::SetupModelForRendering+0x1A4  (Models.cpp:847)
[01] CPGE!SceneManager::LoadScene+0x3C8          (SceneManager.cpp:412)
...

=== Breadcrumb Trail ===
-> SceneManager::LoadScene
-> Models::SetupModelForRendering
```

The **breadcrumb trail** is built from `RECORD_FUNCTION_CALL()` macros placed in high-level functions throughout the engine. Each call records the function name to a ring buffer. When a crash occurs, the ring buffer's contents are dumped — providing the sequence of high-level function entries that led to the fault, even if the exact faulting instruction is deep inside a system library with no symbols.

This is a `_DEBUG`-only feature. In Release builds, the handler is disabled and no log is written, keeping Release executables clean of diagnostic overhead.

#### Colour-Coded Shader Fallbacks

When a shader fails to compile or link at runtime (due to a syntax error, a missing constant buffer, or an API incompatibility), rather than producing a black screen or silent incorrect rendering, CPGE substitutes a **fallback shader** that renders affected objects in a distinctive bright colour (typically magenta or solid red). This makes shader failures immediately visible on screen without needing to examine log files — the developer can see at a glance which objects are affected and which are rendering correctly.

The specific error message from the shader compiler is simultaneously written to the Debug log at `LOG_ERROR` severity, providing the exact line number, column, and description of the compilation failure.

#### Per-Frame Diagnostic Overlay

When `showDebugInfo` is set to `true` in `GameConfig.cfg`, the engine renders a per-frame diagnostic overlay on screen using the Direct2D text layer. The overlay displays:

- **Engine name and version** — `CPGE Windows DirectX 11 v0.0.1682`
- **Frame time** — current frame delta time in milliseconds.
- **Frames per second** — derived from the rolling average delta time.
- **Active scene** — the name of the currently loaded scene.
- **Thread status** — whether the loader thread is active, idle, or waiting.

This overlay requires no debugger, no external profiling tool, and no build configuration change — it is always available in any build where `showDebugInfo = true` in the configuration file.

---

### 3.6 C++17 as the Language Standard

CPGE is written strictly to **C++17** and relies on features from that standard throughout the codebase. The choice of C++17 (rather than C++14, C++20, or C++23) reflects the balance between feature availability, compiler support across all target platforms, and the stability of the standard.

#### C++17 Features Used in CPGE

- **`std::filesystem`** — used by the asset loader, file I/O system, and configuration manager for all path manipulation, file existence checks, and directory operations. Eliminates platform-specific `FindFirstFile` / `opendir` code.
- **`std::optional`** — used in subsystem APIs that may or may not return a result (e.g., finding a named model in the scene, looking up a cached text format).
- **`std::string_view`** — used in hot-path string comparisons and log message formatting to avoid heap allocation for temporary string slices.
- **`if constexpr`** — used in template utility functions to select between code paths at compile time without generating dead code.
- **`std::atomic`** and the C++17 memory model — used throughout for all inter-thread flag communications. The C++17 memory model guarantees the specific ordering semantics (acquire/release) that CPGE's `bIsRendering` drain and `bLoadComplete` signal patterns rely on.
- **Structured bindings (`auto [key, value] = ...`)** — used in range-for loops over `std::unordered_map` entries throughout the caching and registry systems.
- **Inline variables** — used for the asset file tables defined in `Includes.h` (`inline const std::wstring texFilename[]`, `inline const std::filesystem::path xmFilePlaylist[]`) so they can be defined in a header without multiple-definition linker errors across translation units.
- **`std::variant`** and **`std::any`** — available but used sparingly; CPGE prefers explicit tagged unions and typed subsystem interfaces over `std::any` for performance predictability.

#### Why Not C++20 or Later?

C++20 introduces modules, coroutines, concepts, and ranges — features that are compelling but whose compiler support across all of CPGE's target platforms (particularly Android NDK and older Linux toolchains) was not sufficiently mature at the time the language baseline was set. C++17 has full, stable support on MSVC (Visual Studio 2022), GCC 7+, and Clang 5+, which covers every platform CPGE targets. The standard will be revisited as target-platform compiler support matures.

---

### 3.7 Free and Open Source

CPGE is released as **free-to-use** for both personal and commercial purposes under a single, lightweight attribution requirement. The full licence terms are in [Section 37](#37-licensing).

#### What Free-to-Use Means in Practice

- You may use CPGE to build any game or application, for any purpose, without paying any fee, royalty, or subscription.
- You may modify the engine source code to suit your needs.
- You may ship a product built on CPGE to end users (free or commercial) without any per-unit fee.
- You must credit CPGE in your product — the preferred method is displaying the CPGE startup screen; at minimum, a credit in your game's about or credits screen is required.

#### Why This Matters

Commercial game engines typically charge either a perpetual licence fee (paid upfront regardless of whether your game ships) or a royalty on gross revenue above a threshold. Both models add to the developer's cost of production, which then flows through to the consumer as a higher game price. CPGE removes this cost entirely.

The attribution requirement exists not as a commercial condition but as a community one — CPGE is being built in public, for free, and the only thing asked in return is that users acknowledge where the engine came from. This sustains the project's visibility, attracts contributors, and builds the community that will continue improving the engine for everyone.

[Back to Table of Contents](#table-of-contents)

---

## 4. Platform & Operating System Support

CPGE is designed from the ground up to target all major operating systems and graphics APIs through a single, unified C++ codebase. Platform support does not mean shipping a different engine per platform — it means the same source compiles correctly and runs correctly on each target, with platform-specific code isolated behind well-defined preprocessor guards.

---

### 4.1 Platform Overview

| Platform | Supported Renderers | API Surface Available | Status |
| --- | --- | --- | --- |
| **Windows 10 / 11** | DirectX 11, DirectX 12, OpenGL, Vulkan | Full Win32, DirectX, Direct2D, DirectWrite, WIC, Media Foundation, WinSock2, DirectSound, XAudio2 | **Primary — all four backends compiling and running** |
| **Linux (Desktop)** | OpenGL, Vulkan | X11/Wayland, Mesa OpenGL, LunarG Vulkan SDK, ALSA/PulseAudio | Planned — platform defines in place, window and audio backends to be wired |
| **Android** | OpenGL ES, Vulkan | Android NDK, EGL, OpenGL ES 3.x, Vulkan (Android extensions) | Planned — platform defines in place |
| **macOS** | OpenGL | Cocoa, OpenGL (deprecated but present), Core Audio | Planned — platform defines in place |
| **iOS** | OpenGL ES | UIKit, OpenGL ES 3.x, Core Audio | Planned — platform defines in place |

> **Note on platform completion:** Windows is the primary development and testing platform. Every non-Windows platform has its `PLATFORM_*` preprocessor define in place and its rendering API tokens selected in `Includes.h`, but the full set of platform-specific subsystem backends (audio, windowing, input, file I/O) is not yet wired for each target. These will be completed as development progresses to each platform — the architecture is deliberately structured so that adding a platform backend is a matter of filling in platform-specific blocks without restructuring any existing code.

---

### 4.2 How Platform Detection Works

Platform detection is performed entirely through **standard C/C++ predefined compiler macros** — macros that all compliant C++ compilers define automatically based on the compilation target. CPGE does not use any custom detection scripts or CMake platform variables for this purpose. The detection logic lives entirely in [`Includes.h`](../Includes.h) and executes at compile time:

```cpp
#if defined(_WIN64) || defined(_WIN32)
    #define PLATFORM_WINDOWS
#elif defined(__linux__)
    #define PLATFORM_LINUX
#elif defined(__ANDROID__)
    #define PLATFORM_ANDROID
#elif defined(__APPLE__)
    #define PLATFORM_APPLE      // macOS
#elif defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
    #define PLATFORM_IOS
#endif
```

The `PLATFORM_*` token defined here propagates through every source file in the engine as a single, authoritative identifier for the current compilation target. All platform-specific code in subsystems outside `Includes.h` uses only the `PLATFORM_*` tokens — never the raw compiler macros — so if a new platform's detection macro changes or a new platform is added, the change is made in one place only.

#### Compile-Time Platform Identity Strings

`Includes.h` also defines wide-string identity tokens that the engine uses in version overlays and log output:

```cpp
#if defined(PLATFORM_WINDOWS)
    #define PLATFORM_NAME_W L"Windows"
#elif defined(PLATFORM_LINUX)
    #define PLATFORM_NAME_W L"Linux"
#elif defined(PLATFORM_ANDROID)
    #define PLATFORM_NAME_W L"Android"
#elif defined(PLATFORM_APPLE)
    #define PLATFORM_NAME_W L"macOS"
#elif defined(PLATFORM_IOS)
    #define PLATFORM_NAME_W L"iOS"
#endif
```

These combine with `GAME_NAME_W` and `RENDERER_NAME_W` to form the per-frame overlay string: for example, `CPGE Windows DirectX 11 v0.0.1682`.

---

### 4.3 Windows 10 / 11 — Primary Platform

Windows is CPGE's primary development and testing platform. All four rendering backends (DirectX 11, DirectX 12, OpenGL, and Vulkan) are implemented, compiling, and actively maintained for Windows.

#### Why Windows Is Primary

Windows provides the richest graphics API surface of any consumer desktop platform. The DirectX family (Direct3D 11, Direct3D 12, DXGI, Direct2D, DirectWrite, WIC) is available only on Windows, making it uniquely suited as the platform where the widest range of renderer backends can be tested and compared side by side. The Vulkan SDK (LunarG) and OpenGL (via GLEW) are also first-class on Windows, giving CPGE a single platform on which all four backends can be validated before porting begins.

#### Windows API Surface Used by CPGE

The following Windows APIs are actively used by the engine on the Windows platform:

| API / Library | Purpose | Renderer |
| --- | --- | --- |
| **Direct3D 11** (`d3d11.lib`) | 3D rendering device and command context | DX11 |
| **Direct3D 12** (`d3d12.lib`) | Explicit 3D rendering with command queues | DX12 |
| **DXGI 1.2 / 1.6** (`dxgi.lib`) | Swap chain, display mode enumeration, adapter selection | DX11, DX12 |
| **D3D Shader Compiler** (`d3dcompiler.lib`) | Runtime HLSL shader compilation | DX11, DX12 |
| **Direct2D 1.1 / 1.3** (`d2d1.lib`) | 2D vector rendering, GUI and text overlay | All backends |
| **DirectWrite** (`dwrite.lib`) | High-quality font layout and text rendering | All backends |
| **WIC** (`windowscodecs`) | PNG/JPEG texture decoding for loading | All backends |
| **DirectX 11-on-12** (`d3d11on12.h`) | D2D/DWrite overlay layer for DX12 | DX12 |
| **OpenGL 4.x via GLEW** (`glew32s.lib`, `opengl32.lib`) | 3D rendering | OpenGL |
| **Vulkan 1.x** (`vulkan-1.lib`) | 3D rendering with explicit GPU control | Vulkan |
| **XAudio2** (`xaudio2.lib`) | Audio device interface (used by XMMODPlayer) | DX11 |
| **DirectSound** (`dsound.lib`) | SFX buffer playback (used by SoundManager) | DX11, DX12 |
| **Media Foundation** (`mfapi.lib`, `mfreadwrite.lib`) | MP3/M4A streaming, MP4 video decode | All backends |
| **WASAPI** (via Media Foundation) | System audio loopback for ScreenRecorder | All backends |
| **WinSock2** (`ws2_32.lib`) | TCP/UDP network sockets | All (if networking enabled) |
| **IP Helper API** (`iphlpapi.lib`) | Network interface enumeration | All (if networking enabled) |
| **Speech API (SAPI)** | Text-to-speech output | All |
| **Win32 window management** (`user32.lib`, `gdi32.lib`) | Window creation, message loop, WM_* events | All backends |
| **DbgHelp** (`dbghelp.lib`) | Call-stack symbol resolution for crash logs | Debug builds |

#### Minimum Windows Version

CPGE targets **Windows 10 (version 1903 or later)** as the minimum. This minimum is dictated by:

- Direct3D 12 requiring Windows 10 (it is not available on Windows 8.1 or earlier).
- `std::filesystem` requiring a runtime that ships with Windows 10 1903+ for full path-operation support.
- Media Foundation's H.264 hardware decoder being reliably available from Windows 10 onward.

Windows 11 is fully supported and is the recommended development platform, as it includes the latest DXGI 1.6 features, up-to-date Vulkan ICD (Installable Client Driver) support from GPU vendors, and DirectX Agility SDK support for the latest DX12 features.

#### Windows Build Process

See [Section 5](#5-build-system--compilation) for the full build procedure. In summary: install Visual Studio 2022 with the Desktop C++ workload, run `cmake-build.bat <Renderer> <Config>`, and the corresponding executable is produced in `build\<Renderer>\<Config>\`.

---

### 4.4 Linux (Desktop) — Planned

Linux support targets **mainstream x86-64 desktop distributions** — Ubuntu 22.04 LTS and later, Fedora 38+, Debian 12+, and any distribution shipping GCC 7+ or Clang 5+ with a current Mesa or vendor GPU driver.

#### Planned Renderers on Linux

| Renderer | API | Notes |
| --- | --- | --- |
| **OpenGL** | OpenGL 4.x via Mesa or vendor driver | Mature, well-supported on all Linux GPU drivers |
| **Vulkan** | Vulkan 1.x via LunarG SDK or Mesa-Vulkan | Available on AMD (RADV), NVIDIA (proprietary), Intel (ANV) |

DirectX is not available on Linux natively. The DirectX 11 and 12 backends will not be ported to Linux — OpenGL and Vulkan cover the Linux platform fully.

#### What Needs to Be Wired for Linux

The following subsystem components need Linux-specific implementations before the Linux build is production-ready:

- **Windowing** — replace Win32 window creation (`CreateWindowEx`, `WM_*` message loop) with X11 (`XOpenDisplay`, `XCreateWindow`) or Wayland (`wl_display_connect`, `wl_surface_create`). GLFW (already a dependency for OpenGL on Windows) provides a cross-platform window abstraction that will handle this for the OpenGL build. The Vulkan build will use `VK_KHR_xcb_surface` or `VK_KHR_wayland_surface`.
- **Audio** — replace DirectSound and XAudio2 with ALSA (low-level, for embedded-style targets) or PulseAudio (for desktop Linux). The SoundManager's WAV buffer filling logic is platform-independent; only the output device interface needs replacing.
- **Input** — replace `GetAsyncKeyState` / `WM_KEYDOWN` with X11 `XQueryKeymap` / `XNextEvent`. The `KeyboardHandler` already has platform-specific blocks stubbed.
- **File paths** — `std::filesystem` is platform-independent; the `./Assets/` relative path convention will work on Linux without changes.
- **TTS** — replace the Windows SAPI COM calls with a Linux TTS engine such as eSpeak or Festival.

#### Linux CMakeLists.txt

A `Linux/CMakeLists.txt` is already present in the repository, providing the starting point for Linux build configuration. The renderer selection mechanism, source file list, and shader pipeline are structurally identical to the Windows version.

---

### 4.5 Android — Planned

Android support targets **API level 21 (Android 5.0 Lollipop) or later** on ARM64 hardware, using the Android NDK (Native Development Kit) for C++ compilation.

#### Planned Renderers on Android

| Renderer | API | Notes |
| --- | --- | --- |
| **OpenGL ES 3.x** | via EGL (Embedded-system Graphics Library) | Supported on all Android devices with API 21+ |
| **Vulkan** | Vulkan 1.0+ via Android Vulkan extensions | Available on Android API 24+ with Vulkan-capable GPU |

#### Android-Specific Considerations

- **EGL for OpenGL ES** — on Android, OpenGL ES contexts are created through EGL rather than WGL (Windows) or GLX (Linux). EGL initialises the display (`eglGetDisplay`), creates a window surface (`eglCreateWindowSurface`), and binds the context (`eglMakeCurrent`). The OpenGL renderer's window-management block will be wrapped in `#if defined(PLATFORM_ANDROID)` guards to swap in EGL calls.
- **Android Activity lifecycle** — the Android NDK's `ANativeActivity` interface maps Android lifecycle events (`onStart`, `onStop`, `onResume`, `onPause`, `onWindowFocusChanged`, `onInputQueueCreated`) to the engine's own lifecycle signals. The render loop must handle `onPause` (suspend rendering, release EGL context) and `onResume` (recreate EGL context, reload GPU resources) correctly.
- **Vulkan extensions on Android** — the Vulkan surface extension for Android is `VK_KHR_android_surface`. The Vulkan renderer's instance creation will add this extension when `PLATFORM_ANDROID` is defined.
- **Audio on Android** — the SoundManager and XMMODPlayer will use Android's **AAudio** (API 26+) or **OpenSL ES** (API 21+) as the audio output backend instead of DirectSound/XAudio2.
- **File access on Android** — the Android asset manager (`AAssetManager`) must be used for files packaged in the APK. Files in the app's internal storage (`/data/data/<package>/`) can be accessed via normal `std::filesystem` calls after the path is resolved.
- **Crash logging** — `ExceptionHandler` on Android uses NDK signal handlers and resolves stack frames via `/proc/self/maps`, writing the crash log to `/data/local/tmp/Except-CallStack.log`.

---

### 4.6 macOS — Planned

macOS support targets **macOS 12 Monterey or later** on Apple Silicon (ARM64) and Intel x86-64 hardware.

#### Planned Renderer on macOS

| Renderer | API | Notes |
| --- | --- | --- |
| **OpenGL** | OpenGL 4.1 (Apple's maximum supported version) | Available via the Cocoa `NSOpenGLView` or GLFW |

> **Note on Metal:** Apple deprecated OpenGL on macOS in 2018 and recommends Metal as the native graphics API. However, CPGE's current scope does not include a Metal backend — the OpenGL path covers macOS sufficiently for the development target games. A Metal backend may be added in a future release if macOS becomes a primary distribution target.

#### macOS-Specific Considerations

- **Maximum OpenGL version** — Apple's OpenGL implementation is capped at **OpenGL 4.1** and does not support all OpenGL 4.x extensions available on Windows/Linux. CPGE's OpenGL renderer must be careful not to rely on functionality above 4.1 in code paths that will run on macOS.
- **Cocoa window creation** — macOS applications use the Cocoa framework (`NSApplication`, `NSWindow`, `NSOpenGLView`) for windowing and event handling. GLFW abstracts this and is the recommended window management layer for the macOS build.
- **Audio on macOS** — DirectSound and XAudio2 are not available. The audio backends will use **Core Audio** (`AudioUnit`, `AudioToolbox`) or a cross-platform abstraction over it.
- **SAPI TTS replacement** — macOS provides a native TTS API through `NSSpeechSynthesizer` (deprecated in macOS 14) or the newer `AVSpeechSynthesizer`. The `TTSManager` will select the correct backend via `#if defined(PLATFORM_APPLE)`.
- **File paths** — macOS uses POSIX paths. `std::filesystem` handles this transparently. Asset directories will resolve to the application bundle's `Contents/Resources/` folder.

---

### 4.7 iOS — Planned

iOS support targets **iOS 14.0 or later** on ARM64 iPhone and iPad hardware.

#### Planned Renderer on iOS

| Renderer | API | Notes |
| --- | --- | --- |
| **OpenGL ES 3.x** | via EGL / `EAGLContext` | Available on all iOS 14+ devices |

#### iOS-Specific Considerations

- **App Sandbox** — iOS applications run in a strict sandbox. File system access is limited to the app's designated directories. The `ExceptionHandler`'s crash log is written to `$TMPDIR` (the app's sandbox temp folder, which is guaranteed writable). All asset files must be packaged within the app bundle.
- **OpenGL ES context** — iOS uses `EAGLContext` (part of the `GLKit` framework) for OpenGL ES context creation, analogous to EGL on Android. The `EAGL` context must be destroyed and recreated on certain application lifecycle events (entering background, memory pressure warnings).
- **Audio** — `AVAudioEngine` or `AVAudioPlayer` (part of the `AVFoundation` framework) will replace DirectSound/XAudio2 for the iOS audio backend.
- **App Store requirements** — the iOS App Store SDK (`StoreKit`) is already planned for inclusion alongside the Steam SDK for the commercial release. It is excluded from all current builds.
- **Input** — on iOS, touch events replace keyboard and mouse input. The `KeyboardHandler` will receive touch-mapped virtual key events from UIKit (`UITouch`, `UIEvent`) that have been translated to the engine's standard key codes, allowing game logic that uses `IsKeyDown()` to work without modification.

---

### 4.8 Renderer-to-Platform Mapping

Not every renderer is available on every platform. The table below shows the complete supported mapping:

| Renderer | Windows | Linux | Android | macOS | iOS |
| --- | --- | --- | --- | --- | --- |
| DirectX 11 | Yes | No | No | No | No |
| DirectX 12 | Yes | No | No | No | No |
| OpenGL 4.x | Yes | Yes | No | Yes (4.1 max) | No |
| OpenGL ES 3.x | No | No | Yes | No | Yes |
| Vulkan | Yes | Yes | Yes (API 24+) | No | No |

This means a game targeting Windows, Linux, and Android can be covered entirely by the Vulkan backend — the same Vulkan source compiles for all three. A game targeting macOS and iOS needs the OpenGL / OpenGL ES path. A game targeting Windows exclusively has the full choice of all four backends.

---

### 4.9 Writing Platform-Safe Code

Any code added to CPGE that uses platform-specific APIs must be wrapped in the appropriate `#if defined(PLATFORM_*)` guard. The same applies to platform-specific file paths, audio calls, input calls, and thread management calls. Unwrapped platform-specific code will cause compilation failures on other platforms and is treated as a bug.

Example of correct platform-guarded code in a shared subsystem file:

```cpp
void SoundManager::InitializeAudioDevice(HWND hwnd)
{
#if defined(PLATFORM_WINDOWS)
    // DirectSound initialisation
    DirectSoundCreate8(nullptr, &m_pDirectSound, nullptr);
    m_pDirectSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);

#elif defined(PLATFORM_LINUX)
    // ALSA / PulseAudio initialisation (to be implemented)

#elif defined(PLATFORM_ANDROID)
    // AAudio / OpenSL ES initialisation (to be implemented)

#elif defined(PLATFORM_APPLE)
    // Core Audio initialisation (to be implemented)

#else
    #error "SoundManager: No audio backend defined for this platform."
#endif
}
```

The `#error` directive at the bottom ensures that if CPGE is compiled on a platform that has not yet had its audio backend implemented, the compiler produces a clear, actionable error rather than silently compiling a build with no audio.

[Back to Table of Contents](#table-of-contents)

---

## 5. Build System & Compilation

CPGE uses **CMake 4.3.2** as its meta-build system, fronted by a convenience batch script (`cmake-build.bat`) that handles renderer selection, `Includes.h` patching, HLSL shader pre-compilation, and the full CMake + MSBuild pipeline. Understanding the build system deeply is a prerequisite for any work that touches source file lists, preprocessor guards, or project structure.

### 5.1 Prerequisites

Before building CPGE on Windows the following tools must be present:

| Tool | Version | Notes |
| --- | --- | --- |
| **CMake** | 4.3.2 | Discovered from `PATH` first; falls back to `D:\Programs\CMake\bin\cmake.exe` |
| **Visual Studio 2022** | Enterprise or Community | Must include the **Desktop Development with C++** workload |
| **MSBuild** | Bundled with VS2022 | Invoked internally by `cmake --build`; never called directly by contributors |
| **Windows 10/11 SDK** | Latest via VS Installer | Provides `fxc.exe` for HLSL shader pre-compilation; required for all DX builds |
| **Vulkan SDK** | Latest LunarG release | Required only for the Vulkan pipeline; provides `vulkan-1.lib`, `shaderc_shared.dll` |
| **GLEW** | 2.x (static, bundled) | Required only for the OpenGL pipeline; `glew32s.lib` pre-bundled under `lib\` |

> **Critical rule:** Never invoke MSBuild directly from a terminal. Always use `cmake-build.bat`. Invoking MSBuild by hand bypasses `Includes.h` patching, and the resulting binary will silently compile against whichever renderer token happened to be active in the header at the time — producing a binary that is either wrong or refuses to link.

### 5.2 Directory Layout

```text
CPGE2026\
├── cmake-build.bat                      ← Always use this to build
├── install-debug.bat                    ← Post-build asset deploy for Debug
├── install-release.bat                  ← Post-build asset deploy for Release
├── CMakeLists.txt                       ← CMake project definition
├── CrossPlatformGameEngine.vcxproj      ← Visual Studio project (IDE builds)
├── Directory.Build.targets              ← VisualD/incremental build fix
├── Includes.h                           ← Master include / renderer token header
├── BuildInfo.h                          ← Authoritative build number (auto-incremented)
├── Version.id                           ← Plain-text build counter
├── *.cpp / *.h                          ← Engine source files (root level)
├── Assets\                              ← Runtime assets: textures, shaders, models, music
│   └── Shaders\                         ← HLSL / GLSL source files
├── Docs\                                ← All subsystem documentation
├── History\                             ← Timestamped backup copies of every modified file
├── include\GL\                          ← Bundled GLEW headers
├── lib\                                 ← Bundled static libraries (glew32s.lib etc.)
├── build\                               ← CMake-generated solutions (auto-created)
│   ├── DX11\Debug\CrossPlatformGameEngine.sln
│   ├── DX11\Release\CrossPlatformGameEngine.sln
│   ├── DX12\Debug\CrossPlatformGameEngine.sln
│   ├── OpenGL\Debug\CrossPlatformGameEngine.sln
│   └── Vulkan\Debug\CrossPlatformGameEngine.sln
└── steam\                               ← Steam SDK (excluded from all builds until release)
```

> **Two-file sync rule:** When adding any new `.cpp` or `.h` file to the engine you must update **both** `CMakeLists.txt` (the `set(SOURCES ...)` block) **and** `CrossPlatformGameEngine.vcxproj` (`<ClCompile>` / `<ClInclude>` item groups). Always grep both files first — duplicate entries cause the `.vcxproj` to fail loading entirely, and an entry missing from `CMakeLists.txt` means CMake silently produces an executable that omits your file without warning.

### 5.3 cmake-build.bat — The Primary Build Script

`cmake-build.bat` is the single authorised entry point for all build operations. It accepts one renderer argument and one optional configuration:

```bat
cmake-build.bat <renderer> [Debug|Release]
```

| Argument | Accepted Values | Default |
| --- | --- | --- |
| Renderer | `dx11`, `dx12`, `opengl`, `vulkan`, `all` | *(required — error if omitted)* |
| Configuration | `debug`, `release` | `Debug` |

Arguments are case-insensitive. Examples:

```bat
:: Most common during development — DX11 Debug
cmake-build.bat dx11 debug

:: Vulkan Release build
cmake-build.bat vulkan release

:: Build all four renderer pipelines in Debug in sequence
cmake-build.bat all debug

:: Wipe all build artefacts and generated solutions
cmake-build.bat clean
```

#### 5.3.1 Single-Renderer Build — Step by Step

When a single renderer is requested (e.g. `cmake-build.bat dx11 debug`), the script executes the following sequence:

1. **Locate CMake.** Searches `PATH` first; falls back to `D:\Programs\CMake\bin\cmake.exe`. Aborts with a clear error if CMake is not found.
2. **Detect the OS.** Sets `DETECTED_OS=Windows` (or `Unknown` on non-Windows). DirectX renderers abort immediately if run on a non-Windows host.
3. **Map the renderer argument** to two internal variables:
   - `RENDERER` — the CMake string (`DX11`, `DX12`, `OpenGL`, `Vulkan`)
   - `RENDERER_DEFINE` — the preprocessor token (`__USE_DIRECTX_11__`, etc.)
4. **Kill stale compiler processes.** Calls the internal `:killcompiler` subroutine, which force-kills any running `cl.exe` and `mspdbsrv.exe` processes via `taskkill /F`. This releases file handles that a prior cancelled or failed build may have locked on `.obj`, `.pch`, or `.lib` files — preventing "file in use" errors at the start of the new build.
5. **Patch `Includes.h`.** Runs a PowerShell one-liner that:
   - Comments out **all** renderer `#define` lines in the Windows block of `Includes.h` using a single regex pass (`DIRECTX_11`, `DIRECTX_12`, `OPENGL`, `VULKAN`, `RADEON`).
   - Uncomments **only** the line for `RENDERER_DEFINE`.
   This makes the header consistent with what the CMake define injects, so the IDE and CMake builds agree on the active renderer. The patch is idempotent and reversible.
6. **Create the build directory** (`build\<Renderer>\<Config>\`) if it does not exist.
7. **Run CMake configure:** `cmake -S . -B build\<Renderer>\<Config> -G "Visual Studio 17 2022" -A x64 -DRENDERER:STRING=<Renderer>`. CMake reads `CMakeLists.txt`, resolves the renderer token, detects Vulkan SDK / GLEW / FXC, and generates the Visual Studio solution.
8. **Run CMake build:** `cmake --build build\<Renderer>\<Config> --config <Config> --parallel`. This internally calls MSBuild with multi-processor compilation (`/MP`). All compilation errors appear in the console.
9. **Run the post-build install script.** If `install-debug.bat` (or `install-release.bat`) exists and `SKIP_INSTALL` is not set, it is called to copy assets and shaders to the output directory. A warning is printed if the install script is absent — the build itself is still reported as successful.

#### 5.3.2 Building All Four Pipelines (`all`)

`cmake-build.bat all debug` (or `release`) iterates over the four renderer names (`dx11 dx12 opengl vulkan`) and calls the script recursively for each one. Key behaviours:

- DirectX pipelines are automatically skipped on non-Windows hosts — they will not error, they will print `[SKIP] dx11 (DirectX not supported on <OS>)`.
- `SKIP_VERSION_INCREMENT` is set on the second and subsequent pipelines so that `BuildInfo.h` is incremented only once per `all` invocation.
- After all pipelines complete, a one-line pass/fail summary is printed per pipeline.
- The post-build install script runs **once** at the end of the `all` sequence — only if every pipeline succeeded.

```text
============================================================
 Building ALL render pipelines  --  Config: Debug
============================================================

  ---- Building dx11 Debug ----
  ...
  ---- Building dx12 Debug ----
  ...
============================================================
 ALL BUILD SUMMARY  --  14/06/2026  13:22:41
   PASSED : dx11 dx12 opengl vulkan
============================================================
```

#### 5.3.3 Clean (`clean`)

`cmake-build.bat clean` deletes:

- The entire `build\` directory tree (all CMake-generated solutions and build artefacts).
- The `x64\` directory (artefacts from direct VS IDE builds).
- The `CrossPla.2bd3f178\` directory (VS incremental build cache).
- Stray `*.pdb`, `*.ilk`, `*.obj`, `*.pch`, and `*.idb` files in the project root.

A clean build is often necessary after switching renderers or after significant source list changes.

### 5.4 Renderer Selection — How It Works End to End

The renderer is selected **at compile time** by exactly one of the following preprocessor tokens:

| Token | Renderer | API Layer |
| --- | --- | --- |
| `__USE_DIRECTX_11__` | DirectX 11 | Direct3D 11, Direct2D 1.1, XAudio2, DirectSound |
| `__USE_DIRECTX_12__` | DirectX 12 | Direct3D 12 with 11on12 bridge for D2D/DWrite |
| `__USE_OPENGL__` | OpenGL | OpenGL 4.x via GLEW (static), GLFW |
| `__USE_VULKAN__` | Vulkan | Vulkan 1.x, DirectXMath, D2D/DWrite overlay via Win32 |

The token travels through the build system in three parallel channels:

**Channel 1 — CMake `add_compile_definitions()`**

`CMakeLists.txt` passes `${RENDERER_DEFINE}` (e.g. `__USE_DIRECTX_12__`) via `target_compile_definitions()`. Every translation unit compiled by CMake sees this definition injected by the compiler, not by any source header. This is the authoritative channel for CMake builds.

**Channel 2 — `Includes.h` header patch**

`cmake-build.bat` patches `Includes.h` via PowerShell so that the `#define` matching the requested renderer is uncommented and all others are commented out. This keeps the header consistent for IDE IntelliSense and for any tool that reads the source directly. The block that is patched looks like:

```cpp
// ── Renderer selection (patched by cmake-build.bat) ─────────────────────
    #define __USE_DIRECTX_11__
//#define __USE_DIRECTX_12__
//#define __USE_OPENGL__
//#define __USE_VULKAN__
```

**Channel 3 — Safety fallback guard in `Includes.h`**

A `#if !defined(...)` block at the bottom of the renderer-selection section issues a hard `#error` if none of the four tokens is defined. This catches the case where a developer builds with an IDE configuration that bypasses both channels 1 and 2 above:

```cpp
#if !defined(__USE_DIRECTX_11__) && !defined(__USE_DIRECTX_12__) && \
    !defined(__USE_OPENGL__)     && !defined(__USE_VULKAN__)
    #error "No renderer selected. Define exactly one __USE_<RENDERER>__ token."
#endif
```

#### How the Token Gates Code in Shared Files

Every file that contains renderer-specific code wraps those blocks with `#if defined(...)` guards. A typical shared file looks like:

```cpp
void MySubsystem::Init()
{
    #if defined(__USE_DIRECTX_11__)
        // D3D11-specific initialisation
    #elif defined(__USE_DIRECTX_12__)
        // D3D12-specific initialisation
    #elif defined(__USE_OPENGL__)
        // GL-specific initialisation
    #elif defined(__USE_VULKAN__)
        // Vulkan-specific initialisation
    #endif
}
```

Files that are entirely renderer-specific (e.g. `DX12Renderer.cpp`, `VULKAN_Renderer.cpp`) are compiled unconditionally — they guard their own internals with the token — but only one of them links meaningful code at a time because the rest become empty translation units guarded at the top of the file.

### 5.5 Debug vs Release Configurations — Compiler Flags in Detail

`CMakeLists.txt` applies the following MSVC compiler and linker options:

#### Always-active flags (Debug and Release)

| Flag | Purpose |
| --- | --- |
| `/W3` | Warning level 3 — reports most significant warnings |
| `/wd4244` | Suppresses C4244 (narrowing conversion) — expected in 3D maths code |
| `/Zc:__cplusplus` | Forces `__cplusplus` to report the actual standard (C++17 = `201703L`), not `199711L` |
| `/MP` | Multi-processor compilation — compiles translation units in parallel |
| `/nologo` | Suppresses the MSVC banner in build output |
| `/Zp16` | Aligns struct members on 16-byte boundaries — required for SIMD/SSE alignment of `XMMATRIX` and similar types |
| `/sdl` | Enables additional Security Development Lifecycle checks |
| `UNICODE`, `_UNICODE` | Enables the wide-character Win32 API surface (`wchar_t` strings throughout |
| `GLEW_STATIC` | Selects `glew32s.lib` (static) over the DLL import, preventing `__declspec(dllimport)` mismatches |

#### Debug-only flags

| Flag | Purpose |
| --- | --- |
| `/MDd` | Links the debug CRT (`msvcrtd.dll`) — enables heap validation and iterator checking |
| `/Od` | Disables all optimisation — ensures debugger step-through maps 1:1 to source |
| `/FS` | Forces synchronous PDB writes — prevents simultaneous write conflicts during parallel `/MP` builds |
| `/DEBUG` | Generates full PDB symbols for the linker output |
| `/ASSEMBLYDEBUG` | Embeds `DebuggableAttribute` to assist managed/mixed-mode debugging |
| `_DEBUG` | Activates `LOG_INFO`/`LOG_WARNING` log output, assert macros, the ExceptionHandler crash log, and the diagnostic overlay |
| `/MAP` | Generates a `.map` file next to the executable — useful for tracking symbol sizes |

#### Release-only flags

| Flag | Purpose |
| --- | --- |
| `/MD` | Links the release CRT (`msvcrt.dll`) |
| `/O2` | Maximum speed optimisation (enables `/Oi`, `/Ot`, and loop unrolling) |
| `/Oi` | Replaces standard library calls with compiler intrinsics where possible |
| `/Ob2` | Aggressive inlining — functions marked `inline` or `__forceinline` are expanded |
| `/GF` | Eliminates duplicate string constants (read-only string pooling) |
| `/GT` | Enables fibre-safe thread-local storage optimisation |
| `INTERPROCEDURAL_OPTIMIZATION` | Whole-program optimisation (LTCG) — the linker re-optimises across translation unit boundaries |
| `/OPT:REF` | Removes unreferenced functions and data from the linked binary |
| `/OPT:ICF` | Collapses identical COMDAT sections (function folding) |
| `NDEBUG` | Disables `assert()`, `_DEBUG` log output, crash log, and diagnostic overlays |

> **LIBCMT suppression:** `glew32s.lib` is built with the `/MT` static CRT. CPGE uses `/MD`. Without the `/NODEFAULTLIB:LIBCMT` linker flag the linker would emit LNK4098 warnings about conflicting CRT libraries. This flag is applied in both Debug and Release to suppress the noise without changing behaviour.

### 5.6 CMakeLists.txt — Structure and Role

`CMakeLists.txt` is the authoritative project description that CMake translates into a Visual Studio solution. Its sections, in order:

1. **Game identity** — `GAME_NAME` and `RENDERER` CMake cache variables. These drive the output executable name (`${RENDERER_PREFIX}${GAME_NAME}`) and the preprocessor define.
2. **Source list** — the `set(SOURCES ...)` block lists every `.cpp` and `.rc` file. All four renderer source trees are always listed; the renderer token in each file guards which code is actually compiled.
3. **`add_executable()`** — declares `CrossPlatformGameEngine WIN32 ${SOURCES}`. The `WIN32` flag sets the entry point to `WinMain` instead of `main`, suppressing the console subsystem window for Release builds (Debug still spawns a console via `_CONSOLE`).
4. **Output name** — `set_target_properties(...OUTPUT_NAME "${EXE_OUTPUT_NAME}")` produces the renderer-prefixed executable name.
5. **Vulkan SDK detection** — a four-step search: `VULKAN_SDK` env var → drive-root scan (`C:\..G:\VulkanSDK\*`) → `find_package(Vulkan)`. If Vulkan is required but not found, `FATAL_ERROR` prints a detailed install guide. The Vulkan SDK version is extracted from the path and printed to the console during configure.
6. **OpenGL version detection** — scans `include/GL/glew.h` for `GLEW_VERSION_X_Y` defines to determine the maximum OpenGL API version exposed by the bundled headers. On Linux/macOS, `pkg-config` is also queried.
7. **Include and library directories** — the project root (`CMAKE_CURRENT_SOURCE_DIR`), the bundled headers (`include\`), and the Vulkan SDK include path are added. The bundled `lib\` directory and Vulkan `Lib\` are added to the linker search path.
8. **shaderc linkage** — for Vulkan builds on Windows, `shaderc_sharedd.lib` (Debug) or `shaderc_shared.lib` (Release) is linked. A `POST_BUILD` command copies the matching `shaderc_shared[d].dll` from the Vulkan SDK `Bin\` directory to the executable output folder so the exe finds it at runtime.
9. **MSVC compiler options** — `/W3`, `/Zp16`, `/MP`, per-config `/MDd` vs `/MD`, optimisation and link flags as described in section 5.5.
10. **HLSL shader pre-compilation** — a `PRE_BUILD` custom command compiles all `Assets\Shaders\*.hlsl` files to `.cso` bytecode using `fxc.exe` (see section 5.9).
11. **Windows system libraries** — `d3d11.lib`, `d3d12.lib`, `dxgi.lib`, `d2d1.lib`, `dwrite.lib`, `dsound.lib`, `winmm.lib`, `opengl32.lib`, `vulkan-1.lib`, and others are linked depending on the renderer.

### 5.7 Debug vs Release — Behavioural Differences at Runtime

Beyond compiler flags, Debug and Release produce different engine behaviours at runtime:

| Behaviour | Debug | Release |
| --- | --- | --- |
| Optimisation | None (`/Od`) — source-level debugger step-through | Full speed (`/O2`, LTCG, inlining) |
| CRT | Debug CRT — heap validation, iterator checking | Release CRT — no overhead |
| PDB symbols | Full PDB — breakpoints, watch windows, call stacks in VS | Stripped — no symbols |
| ExceptionHandler | Active — writes crash log with 25 stack frames + breadcrumb trail to disk | Inactive |
| Diagnostic overlay | Active — build number, FPS, renderer name, frame time on screen | Inactive |
| `LOG_INFO` / `LOG_WARNING` | Printed to debug output and log file | Silent |
| `LOG_ERROR` / `LOG_CRITICAL` | Active in both configurations | Active in both |
| Assert macros | Trigger breakpoint + log on failure | Compiled out |
| `_DEBUG` macro | Defined — enables all conditional debug code | Not defined |
| `NDEBUG` macro | Not defined | Defined — disables standard library assertions |
| Binary size | Larger — debug symbols, no dead-code stripping | Smallest — `/OPT:REF /OPT:ICF` remove unused code |
| Map file | Generated — `CrossPlatformGameEngine.map` at project root | Not generated |

### 5.8 Visual Studio IDE Builds

The CMake-generated solution files under `build\<Renderer>\<Config>\CrossPlatformGameEngine.sln` can be opened in Visual Studio 2022 directly. When opened:

- The `.vcxproj` already carries the correct include paths, preprocessor defines (including the renderer token injected by CMake), and library link paths for the selected renderer.
- Building from within the IDE invokes MSBuild via the same `.vcxproj`; the renderer token is embedded so no manual `Includes.h` edits are needed for that session.
- Switching renderers from the IDE requires either:
  1. Generating a new CMake solution via `cmake-build.bat <new-renderer> debug` (recommended), or
  2. Manually editing the `Preprocessor Definitions` field in the project properties and uncommenting the matching `#define` in `Includes.h`.

#### `Directory.Build.targets` — the VisualD Fix

A `Directory.Build.targets` file in the project root patches the MSBuild property graph to resolve an incompatibility between CMake-generated `.vcxproj` files and the VisualD extension (a D/C++ hybrid IDE plugin). Without this file, incremental builds from the Visual Studio IDE can silently recompile all translation units even when no source has changed. The fix is transparent — it requires no action from contributors.

#### IDE-Only Builds (Bypassing cmake-build.bat)

If `cmake-build.bat` has not been run first, the `Includes.h` header will not have been patched. In that case, manually uncomment exactly one renderer `#define` in `Includes.h` before building:

```cpp
    #define __USE_DIRECTX_11__   // ← uncomment this line for a DX11 IDE build
//#define __USE_DIRECTX_12__
//#define __USE_OPENGL__
//#define __USE_VULKAN__
```

Leaving more than one uncommented results in a compile error — all shared files assume exactly one token is active.

### 5.9 HLSL Shader Pre-Compilation

All HLSL shaders in `Assets\Shaders\` are compiled to DirectX Shader Object (`.cso`) bytecode as a `PRE_BUILD` step before any C++ source file is compiled. The compiler used is `fxc.exe` (the DirectX effect compiler, bundled with the Windows SDK).

**`fxc.exe` detection — four-priority search chain**

`CMakeLists.txt` searches for `fxc.exe` in the following order, stopping at the first hit:

1. **VS developer environment variables** — `WindowsSdkBinPath` and `WindowsSdkDir` + `WindowsSDKVersion`, set by `vcvars64.bat`. These always point to the exact SDK version in use.
2. **CMake VS-generator detected version** — `CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION` is used to build a path under both `Program Files` and `Program Files (x86)`.
3. **Windows registry** — `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows Kits\Installed Roots\KitsRoot10` is read to find the SDK install root; all versioned `bin\10.*\x64\` subdirectories are searched.
4. **Broad glob** — both `Program Files` roots are searched recursively for `Windows Kits\10\bin\10.*\x64\fxc.exe`.

If `fxc.exe` is still not found after all four searches, CMake issues a `FATAL_ERROR` with a detailed install guide explaining how to add it via the Visual Studio Installer or standalone SDK download.

#### Shader Compilation at Build Time

Each `.hlsl` file is compiled with a command of the form:

```bat
fxc.exe /T vs_5_0 /E VSMain /Fo output\VertexShader.cso Assets\Shaders\VertexShader.hlsl
fxc.exe /T ps_5_0 /E PSMain /Fo output\PixelShader.cso  Assets\Shaders\PixelShader.hlsl
```

The `/T` flag specifies the shader target profile (`vs_5_0` = Vertex Shader Model 5, `ps_5_0` = Pixel Shader Model 5). The `.cso` files are written to the executable output directory so the engine can load them at startup with `D3DReadFileToBlob()`.

Vulkan shaders (GLSL/SPIR-V) are handled differently: they are compiled at runtime by `shaderc` when the Vulkan renderer initialises.

### 5.10 Output Executables

After a successful build, the executable is placed in:

```text
build\<Renderer>\<Config>\<ExeName>\<Config>\<ExeName>.exe
```

The executable name is composed of the renderer prefix and the `GAME_NAME` CMake variable:

| Renderer | Executable |
| --- | --- |
| DirectX 11 | `DXCPGE.exe` |
| DirectX 12 | `DX12CPGE.exe` |
| OpenGL | `OpenGLCPGE.exe` |
| Vulkan | `VulkanCPGE.exe` |

`.cso` shader files are placed alongside the executable. `shaderc_shared[d].dll` is copied there automatically by the `POST_BUILD` step for Vulkan builds.

To rename the shipped game, change the `GAME_NAME` CMake cache variable (`CMakeLists.txt` line 9), update the matching `#define GAME_NAME` in `Includes.h`, and update `<GameName>` in `CrossPlatformGameEngine.vcxproj`.

### 5.11 Adding New Source Files

Any new `.cpp` or `.h` file added to the engine requires **both** of the following edits before the next build:

1. **`CMakeLists.txt`** — add the filename to the `set(SOURCES ...)` block. Without this, CMake's generated solution omits the file entirely and the executable silently links without it.
2. **`CrossPlatformGameEngine.vcxproj`** — add a matching `<ClCompile Include="NewFile.cpp" />` or `<ClInclude Include="NewFile.h" />` item. Without this, Visual Studio IDE builds cannot see or compile the file, and the project may fail to load if a duplicate is introduced.

Always `grep` both files for the new filename before adding it — adding a filename that already exists creates a duplicate entry, which causes the `.vcxproj` to fail loading entirely.

### 5.12 Build Troubleshooting

| Symptom | Likely Cause | Fix |
| --- | --- | --- |
| `#error "No renderer selected"` | No renderer token defined — build bypassed `Includes.h` patching | Uncomment the correct `#define` in `Includes.h` or run `cmake-build.bat` |
| `LNK1104: cannot open file 'vulkan-1.lib'` | Vulkan SDK not installed or `VULKAN_SDK` not set | Install LunarG Vulkan SDK; restart terminal; re-run `cmake-build.bat vulkan` |
| `CANNOT Compile Resources - Please Install MSSDK` | `fxc.exe` not found | Add "HLSL Tools" via VS Installer or install the standalone Windows SDK |
| `EBUSY` / file locked after Ctrl+C | `cl.exe` still holds object file handles | `cmake-build.bat` kills `cl.exe` at startup automatically; if running manually, terminate `cl.exe` and `mspdbsrv.exe` via Task Manager |
| `.vcxproj` fails to load | Duplicate `<ClCompile>` or `<ClInclude>` entry | `grep` the `.vcxproj` for the duplicated filename and remove the extra entry |
| Executable links but renders black screen | Wrong renderer token active (e.g. DX11 token with DX12 `.sln`) | Run `cmake-build.bat <correct-renderer>` to regenerate and re-patch |
| `LNK4098: defaultlib 'LIBCMT' conflicts` | GLEW static lib CRT mismatch | Already suppressed via `/NODEFAULTLIB:LIBCMT`; should not appear with current `CMakeLists.txt` |

[Back to Table of Contents](#table-of-contents)

---

## 6. Renderer Architecture — Multi-Backend Overview

CPGE's rendering layer is built around a common abstract interface (`Renderer`) that all four backends implement. Game code, scene management, the FX manager, and the GUI system call through the `Renderer` interface and never reference a backend type directly — the concrete renderer (`DX11Renderer`, `DX12Renderer`, `OpenGLRenderer`, `VulkanRenderer`) is determined at startup from `GameConfig.cfg` and held in a `std::shared_ptr<Renderer> renderer` global.

Every backend shares the same:

- Abstract `Renderer` base class and virtual method table (`Renderer.h`).
- Per-frame render pipeline in a dedicated `*RenderFrame.cpp` file (included into the renderer translation unit at compile time, not as a separate `.obj`).
- Fixed constant-buffer and texture-slot layout (see tables below) so shaders are portable with minimal changes.
- `RendererFactory` (`RendererFactory.cpp`) for instantiation and hardware support checking.
- Public API for camera, models, lighting, FX, 2D text/blit operations, scissor clipping, and screen-mode management.

### 6.1 The Renderer Abstract Base Class

**Source file:** [Renderer.h](Renderer.h)

`Renderer` is a pure-virtual abstract base class. All concrete backends inherit it publicly and override every virtual method. The class is declared with a `protected` default constructor so it can only be instantiated via its derived classes.

#### Key virtual methods

| Method | Purpose |
| --- | --- |
| `Initialize(HWND, HINSTANCE)` | Full device + swap chain + subsystem init for the selected backend |
| `RenderFrame()` | Per-frame render loop — runs as a dedicated renderer thread when `RENDERER_IS_THREAD` is defined |
| `LoaderTaskThread()` | Background IO loader thread entry point — loads textures / geometry off the render thread |
| `Resize(w, h)` | Rebuilds swap chain, RTVs, depth-stencil and all dependent resources for a new window size |
| `SetFullScreen()` / `SetFullExclusive(w,h)` / `SetWindowedScreen()` | Display-mode transitions with fence draining and safe resource teardown |
| `WaitForGPUToFinish()` | Blocks the CPU until all outstanding GPU work is complete — used before Cleanup |
| `DrawRectangle` / `DrawCircle` / `DrawMyText` / `DrawTexture` | 2D drawing primitives dispatched through the Direct2D or OpenGL 2D path |
| `DrawMyTextStyled` | Text rendering with per-call `TextRenderStyle` (font name, size, bold, italic, underline, centring) |
| `PushClipRect` / `PopClipRect` | Scissor rectangle management for GUI tab-content clipping |
| `GetDevice()` / `GetDeviceContext()` / `GetSwapChain()` | Return `void*` pointers castable to the backend's COM smart-pointer type |
| `Cleanup()` | Ordered teardown: GPU drain → release resources → null COM pointers |

#### Key public state

| Member | Type | Purpose |
| --- | --- | --- |
| `bIsInitialized` | `std::atomic<bool>` | Set once `Initialize()` completes; guards render-thread startup |
| `bIsDestroyed` | `std::atomic<bool>` | Set when Cleanup begins; prevents double-teardown |
| `bIsMinimized` | `std::atomic<bool>` | Suppresses GPU work when the window is minimised |
| `RenderType` | `RendererType` enum | Identifies the active backend (`RT_DirectX11`, `RT_DirectX12`, `RT_OpenGL`, `RT_Vulkan`) |
| `myCamera` | `Camera` | The 3D camera object — same type used by FX, models, and the scene manager |
| `bWireframeMode` | `bool` | Runtime wireframe toggle via F2; propagates to the 3D rasteriser state |
| `bDebugOSDActive` | `bool` | Runtime diagnostic overlay toggle via F12 |

#### The 2D blit queue

All renderers expose a typed 2D blit queue backed by a `GFXObjQueue[MAX_2D_IMG_QUEUE_OBJS]` array (default capacity: 512 slots). Each entry carries:

- `BlitObj2DType` — category (player, enemy, text, GUI button, progress bar, etc.)
- `BlitObj2DDetails` — position, size, animation frame index, collidability flag
- `BlitPhaseLevel` — render order (1–5); higher phases render on top of lower
- `CanBlitType` — whether the entry is consumed once or persists across frames

The queue is sorted by `BlitPhaseLevel` each frame. The 2D layer renders after all 3D and FX work, ensuring GUI and HUD are always drawn on top.

#### The render loop (7-step pipeline — DX11 reference)

The canonical per-frame order, as documented in [DXRenderFrame.cpp](DXRenderFrame.cpp):

1. **Safety guards + exclusive lock** — acquire `s_renderMutex`, check `bIsInitialized`, `bIsMinimized`, `bResizeInProgress`.
2. **Clear + delta-time** — clear render target and depth-stencil; calculate delta time for animation and FX updates.
3. **Background image** — `RenderBackgroundImage()` via Direct2D before any 3D work (scene-transition guard applied).
4. **3D scene** — `OMSetRenderTargets`, 3D model draw calls, animation, lighting (`RenderGamePlay()`).
5. **FX layer** — `fxManager.Render()` — particles, starfield, warp dot tunnel, fade overlays, and other D3D/D2D effects.
6. **GUI + text + 2D overlays** — Direct2D layer: GUI windows, FPS counter, cursor, recording indicator, debug OSD.
7. **Present** — `IDXGISwapChain::Present(1, 0)` (VSync on) or `Present(0, 0)` (VSync off as configured).

All other backends (DX12, OpenGL, Vulkan) follow the same 7-step ordering, with API-specific equivalents at each step.

### 6.2 Shared Slot Layout

The constant-buffer and texture-slot layout is fixed and identical across all four backends so that shaders can be ported between them with minimal changes.

#### Constant-buffer slots

| Slot | Purpose |
| --- | --- |
| `b0` | World / View / Projection matrix constant buffer |
| `b1` | Per-model light buffer |
| `b2` | Debug information buffer |
| `b3` | Global directional light buffer |
| `b4` | Material properties buffer |
| `b5` | Environment / skybox settings buffer |
| `b6` | Shadow map constant buffer |

#### Texture slots (pixel shader)

| Slot | Purpose |
| --- | --- |
| `t0` | Diffuse / Albedo texture |
| `t1` | Normal map |
| `t2` | Metallic map |
| `t3` | Roughness map |
| `t4` | Ambient Occlusion (AO) map |
| `t5` | Environment / reflection cube map |
| `t6` | Gloss / smoothness map |
| `t7` | Emissive map |
| `t8` | Shadow depth map (PCF) |

### 6.3 DirectX 11 Renderer

**Source files:** [DX11Renderer.cpp](DX11Renderer.cpp) / [DX11Renderer.h](DX11Renderer.h), [DXRenderFrame.cpp](DXRenderFrame.cpp)

The DirectX 11 renderer is the primary, fully-featured backend and the reference implementation against which all subsystems are first developed and tested. It is the most mature pipeline and exercises the widest breadth of the engine's feature set.

#### Device stack

| Object | Role |
| --- | --- |
| `ID3D11Device` | Core GPU device — creates resources (buffers, textures, shaders, state objects) |
| `ID3D11DeviceContext` | Immediate context — submits draw calls, sets pipeline state, maps buffers |
| `IDXGISwapChain1` | DXGI 1.2 flip-model swap chain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`) — low-latency presentation with the DWM compositor |
| `ID3D11RenderTargetView` | View into the back buffer used as the colour render target |
| `ID3D11DepthStencilView` | View into the depth-stencil buffer; recreated on every resize |
| `ID2D1DeviceContext` | Direct2D 1.1 device context sharing the same DXGI surface as the swap chain — zero-copy 2D rendering |
| `IDWriteFactory` | DirectWrite factory for high-quality text layout, font enumeration, and text measurement |

#### 2D/3D layer sharing — zero-copy compositing

The key architectural detail in the DX11 path is that the 3D swap-chain back buffer and the Direct2D render target are the **same DXGI surface**. The DX11 device context writes 3D output to the back buffer; then Direct2D wraps that same surface as a `D2D1_BITMAP_PROPERTIES1` compatible target and draws 2D content directly on top — no GPU-to-CPU readback, no intermediate blit. The surface goes straight to `Present()` with both layers composited on the GPU.

#### Audio integration

The DX11 path initialises both audio subsystems:

- **XAudio2** — used by `XMMODPlayer` for the software XM tracker mixing pipeline. XAudio2 provides a callback-driven audio graph: the XM player submits PCM buffers to an `IXAudio2SourceVoice` which XAudio2 mixes and sends to the master voice and hardware output.
- **DirectSound** — used by `SoundManager` for SFX playback. Each sound effect is a `IDirectSoundBuffer8` with its own volume, pan, and frequency controls.

#### Window resizing

When the window is resized, DX11 executes the following sequence:

1. Set `bResizeInProgress = true` to block the render thread.
2. Drain the GPU via `WaitForGPUToFinish()`.
3. Release the RTV, DSV, and any Direct2D resources holding references to the back buffer.
4. Call `IDXGISwapChain::ResizeBuffers(0, newW, newH, DXGI_FORMAT_UNKNOWN, 0)` — passing 0 for count and format preserves the existing buffer count and format.
5. Recreate RTV, DSV, Direct2D render target, and viewport.
6. Clear `bResizeInProgress` to resume rendering.

#### Thread model

`RenderFrame()` is defined as a dedicated renderer thread when `RENDERER_IS_THREAD` is active in `Renderer.h`. The thread runs continuously; it is paused via `WaitToFinishThenPauseThread()` during scene transitions, resizes, and fullscreen mode changes to prevent GPU race conditions.

A second thread, `LoaderTaskThread()`, handles IO-bound work (texture uploads, geometry uploads, scene cache reads). It runs on the loader thread registered in `ThreadManager` and coordinates with the render thread via `s_loaderMutex` and `bIsRendering` atomic drain patterns.

### 6.4 DirectX 12 Renderer

**Source files:** [DX12Renderer.cpp](DX12Renderer.cpp) / [DX12Renderer.h](DX12Renderer.h), [DX12RenderFrame.cpp](DX12RenderFrame.cpp), [DX12Models.cpp](DX12Models.cpp), [DX12FXManager.cpp](DX12FXManager.cpp)

The DirectX 12 backend implements the full explicit command-queue model that DX12 requires. Unlike DX11 where the driver manages resource state implicitly, DX12 requires the application to track every resource state and issue explicit `ResourceBarrier` transitions.

#### Command recording and submission

Each frame, CPGE:

1. Waits on the per-frame fence to confirm the GPU has finished using the previous frame's command allocator.
2. Calls `ID3D12CommandAllocator::Reset()` to reclaim its memory.
3. Opens the `ID3D12GraphicsCommandList`, records all draw calls (resource barriers, PSO binds, root constant-buffer updates, draw indexed calls), closes the list.
4. Submits the list via `ID3D12CommandQueue::ExecuteCommandLists()`.
5. Calls `IDXGISwapChain3::Present()`.
6. Signals the fence with `ID3D12CommandQueue::Signal()`.

This **double-buffered allocator** pattern — one allocator per back buffer, reset only after its fence is signalled — means the CPU never stalls waiting for the GPU at the start of a new frame unless the GPU is more than one frame behind (which indicates a genuine perf problem).

#### Resource binding

DX12 uses an explicit **descriptor heap** model. CPGE maintains:

- **CBV/SRV/UAV heap** — constant-buffer views (one per constant-buffer slot `b0`–`b6`) and shader-resource views for all textures (`t0`–`t8`) are written as descriptors into this heap at texture-load time.
- **RTV heap** — one render-target view descriptor per back buffer.
- **DSV heap** — one depth-stencil view descriptor.

A **root signature** describes the binding layout expected by the shaders: which descriptor tables map to which parameter slots, and where inline root constants live. All PSOs in CPGE share a single root signature.

**Pipeline State Objects (PSOs)** bundle the vertex and pixel shader bytecode, input layout, blend state, depth-stencil state, rasteriser state, and render-target format into an immutable GPU object. PSOs are created once during `Initialize()` from pre-compiled `.cso` bytecode (see section 5.9) and cached for the application lifetime.

#### 2D overlay — 11on12 bridge

DX12 provides no 2D drawing API. CPGE uses the `D3D11On12CreateDevice()` pattern:

1. A wrapped DX11 device is created over the DX12 command queue at startup.
2. Each back-buffer resource (`ID3D12Resource`) is wrapped via `ID3D11On12Device::CreateWrappedResource()` to produce an `ID3D11Resource` view of it.
3. Before the 2D layer, the DX12 resource is transitioned to `D3D12_RESOURCE_STATE_RENDER_TARGET` and acquired by the 11on12 device.
4. Direct2D and DirectWrite render to the wrapped resource.
5. The resource is released back to DX12 and transitioned to `D3D12_RESOURCE_STATE_PRESENT` before `IDXGISwapChain::Present()`.

This produces the same 2D text and GUI quality as the DX11 path with no visible difference to the end user.

#### Performance-critical design decisions

- All COM objects that would otherwise be allocated per frame inside draw calls (format converters, text format objects, brush objects) are hoisted to class-member `ComPtr<>` fields and initialised once. Explicit `Reset()` is called in `Cleanup()` and `Resize()`. This eliminated both per-frame heap churn and the clean-exit crashes that occurred when the allocator loop ran during shutdown.
- `RefreshDX12Textures()` is called after any post-`Setup()` texture rebind to re-upload SRV descriptors to the CBV/SRV/UAV heap. Missing this call produces a grey render (sampling from an empty descriptor slot).

### 6.5 OpenGL Renderer

**Source files:** [OpenGLRenderer.cpp](OpenGLRenderer.cpp) / [OpenGLRenderer.h](OpenGLRenderer.h), [OpenGLRenderFrame.cpp](OpenGLRenderFrame.cpp), [OpenGLModels.cpp](OpenGLModels.cpp), [OpenGLFXManager.cpp](OpenGLFXManager.cpp), [OpenGLCamera.cpp](OpenGLCamera.cpp)

The OpenGL backend uses OpenGL 4.x via **GLEW 2.x** (statically linked, `GLEW_STATIC` — uses `glew32s.lib`) and a GLFW window context for window management and OS event dispatch. OpenGL was chosen as the cross-platform path because it runs on Windows, Linux, Android, macOS, and iOS without requiring a vendor-specific SDK.

#### Buffer objects

All mesh geometry is stored in OpenGL buffer objects:

- **VAO (Vertex Array Object)** — captures the vertex attribute layout (position, normal, UV, tangent) so it only needs to be described once per mesh.
- **VBO (Vertex Buffer Object)** — the raw interleaved vertex data on the GPU.
- **EBO (Element Buffer Object)** — 32-bit index buffer; all mesh draws use `glDrawElements()`.

#### Uniform Buffer Objects (UBOs) and std140 layout

Shader constants are uploaded via UBOs that mirror the DX11 constant-buffer slots. The `std140` layout rule governs how GLSL places struct members:

- Scalar arrays have a base alignment and stride of **16 bytes per element** regardless of the element's natural size. A `float[4]` array occupies 64 bytes, not 16.
- `mat4` members are column-major in GLSL but CPGE's `ModelInfo` matrices are stored row-major (matching DirectX convention). Matrices are uploaded **raw without an implicit transpose** — the shader is written to match the memory layout, not the other way around.
- This convention is documented in engine memory (see [OpenGL matrix & texture conventions](../memory/project_opengl_matrix_texture_conventions.md)).

#### GLSL shader pipeline

GLSL shader source files (`.glsl`) live in `Assets/Shaders/`. At startup, the OpenGL renderer:

1. Reads each `.glsl` file from disk via `FileIO`.
2. Compiles it with `glShaderSource()` + `glCompileShader()`.
3. Links pairs into a `GLuint` program object with `glLinkProgram()`.
4. On compile or link error, queries the info log via `glGetShaderInfoLog()` / `glGetProgramInfoLog()`, reports it through the `Debug` system, and substitutes the colour-coded error shader (solid magenta) so the application remains running.
5. Queries all uniform block indices and binds them to the UBO binding points matching the DX11 slot convention.

#### Texture management and the RefreshOpenGLTextures() rule

Texture units follow the same slot assignment as DX11: `GL_TEXTURE0` = diffuse, `GL_TEXTURE1` = normal map, and so on up to `GL_TEXTURE8` = shadow depth. After any texture rebind operation (new texture loaded, scene reload, cache restore), `RefreshOpenGLTextures()` **must** be called to re-apply all sampler bindings. Omitting this call causes samplers to read from previously-bound textures or stale state.

UV wrap modes per texture are controlled via `ModelInfo::uvWrapU` / `ModelInfo::uvWrapV` (values map to `GL_REPEAT`, `GL_CLAMP_TO_EDGE`, etc.), consistent with the DX11 and Vulkan paths. The wrap mode is applied via `glTexParameteri()` on the texture object, not the sampler.

#### 2D layer

On Windows, the OpenGL renderer uses the same D2D/DWrite 11on12 overlay pattern as DX12 — DXGI interop is available even for an OpenGL context. On Linux and Android, a CPU-rasterised Direct2D fallback path is used (stub implementation pending full port).

#### Additional linking (Windows)

Beyond `opengl32.lib` and `glew32s.lib`, the OpenGL build links:

- `glu32.lib` — GLU utilities.
- `gdiplus.lib` — GDI+ used by the texture loader for non-DDS image formats.
- `dxgi.lib` — for GPU adapter enumeration and VRAM capacity reporting (used in the diagnostics overlay).

### 6.6 Vulkan Renderer

**Source files:** [VULKAN_Renderer.cpp](VULKAN_Renderer.cpp) / [VULKAN_Renderer.h](VULKAN_Renderer.h), [VULKAN_RenderFrame.cpp](VULKAN_RenderFrame.cpp), [VulkanModels.cpp](VulkanModels.cpp), [VULKAN_FXManager.cpp](VULKAN_FXManager.cpp), [VulkanCamera.cpp](VulkanCamera.cpp)

The Vulkan backend uses the LunarG Vulkan SDK (`vulkan-1.lib`) and is the most explicitly managed of the four pipelines. Every resource, synchronisation primitive, and command buffer must be created and destroyed by the application.

#### Initialisation chain

The Vulkan backend initialises in this order:

1. **`VkInstance`** — created with the application name, engine name, and required instance extensions (`VK_KHR_surface`, `VK_KHR_win32_surface` on Windows). In Debug builds, `VK_EXT_debug_utils` is added and `VK_LAYER_KHRONOS_validation` is enabled via `k_validationLayers`.
2. **Debug messenger** — `VkDebugUtilsMessengerEXT` is registered to route Vulkan validation errors and warnings through the `Debug` system's log. Only active when `k_enableValidation = true` (Debug builds only, controlled by `_DEBUG`).
3. **`VkSurfaceKHR`** — created via `vkCreateWin32SurfaceKHR` on Windows; platform-specific surface creation on Linux/Android.
4. **Physical device** — all available GPUs are enumerated; a score function prefers discrete GPUs over integrated ones, and verifies required device extensions (`VK_KHR_swapchain`) and queue families.
5. **Logical device** (`VkDevice`) — created with graphics queue family and presentation queue family. If they are the same family (typical on Windows), one queue is used for both.
6. **`VkSwapchainKHR`** — triple-buffered where the surface supports it; surface format `VK_FORMAT_B8G8R8A8_SRGB` / `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`; present mode `VK_PRESENT_MODE_FIFO_KHR` (VSync) or `VK_PRESENT_MODE_MAILBOX_KHR` (low-latency) as configured.
7. **Render pass** — a single render pass with a colour attachment (swap-chain image format) and a depth attachment (`VK_FORMAT_D32_SFLOAT`).
8. **Framebuffers** — one `VkFramebuffer` per swap-chain image, each referencing its image view and the shared depth image view.
9. **Command pools + buffers** — one `VkCommandPool` per thread; one primary `VkCommandBuffer` per frame in flight.
10. **Descriptor set layout** — describes the binding schema: UBO at binding 0, combined image-samplers at bindings 1–9 (mirroring `t0`–`t8`).
11. **Descriptor pool + sets** — one descriptor set per swap-chain image; updated via `vkUpdateDescriptorSets()` when textures are bound.
12. **Pipeline layout + `VkPipeline`** — the pipeline encodes vertex input state, rasteriser state, depth-stencil state, and colour blend state in an immutable object. GLSL shaders are compiled to SPIR-V at runtime via `shaderc` (see below).

#### GLSL to SPIR-V — runtime shader compilation

Unlike DX11/DX12 (which load pre-compiled `.cso` bytecode), the Vulkan renderer compiles GLSL shaders to SPIR-V at startup using **`shaderc`** (the runtime shader compiler bundled in the LunarG Vulkan SDK):

1. GLSL source is read from `Assets/Shaders/`.
2. `shaderc::Compiler::CompileGlslToSpv()` produces a `std::vector<uint32_t>` SPIR-V binary.
3. A `VkShaderModule` is created from the SPIR-V binary.
4. The shader module is referenced in the `VkPipelineShaderStageCreateInfo` for the pipeline.
5. The module may be destroyed after the pipeline is created.

If `shaderc` is not present (e.g. Vulkan SDK not installed), the renderer falls back to loading pre-compiled SPIR-V `.spv` files if they exist alongside the `.glsl` source.

#### Synchronisation model

Per-frame synchronisation uses both semaphores and fences:

- **`imageAvailableSemaphore`** — signalled by `vkAcquireNextImageKHR()` when a swap-chain image is ready to render into.
- **`renderFinishedSemaphore`** — waited on by `vkQueuePresentKHR()` to ensure rendering is complete before presentation.
- **In-flight fence** — signalled by the queue submission; waited on at the start of the next frame that would reuse the same command buffer. This prevents overwriting a command buffer the GPU is still reading.

#### 2D overlay on Windows

The Vulkan render path on Windows uses the same D2D/DWrite overlay as the DX12 path: a DX11on12 device is created over the Vulkan queue (via a DXGI-compatible surface), and Direct2D renders text and GUI into the shared surface before `vkQueuePresentKHR()`.

#### Render parity requirement

The Vulkan render frame ([VULKAN_RenderFrame.cpp](VULKAN_RenderFrame.cpp)) must remain in structural sync with the DX11 render frame ([DXRenderFrame.cpp](DXRenderFrame.cpp)). Whenever a visual feature is added to the DX11 path (starfield, console overlay, loading text, etc.) it must be ported to the Vulkan path before that task is marked complete. The 7-step pipeline order is the same in both files.

### 6.7 RendererFactory — Instantiation and Hardware Checking

**Source file:** [RendererFactory.cpp](RendererFactory.cpp)

`RendererFactory.cpp` contains a single function: `CreateRendererInstance()`. It is called during engine startup, before `Initialize()`, to select and instantiate the correct concrete renderer.

#### How instantiation works

1. `Configuration::ValidateRendererForPlatform(config.myConfig.rendererType)` converts the integer from `GameConfig.cfg` to a platform-legal renderer index. On Linux/Android, for example, DX indices 0 and 1 are re-mapped to OpenGL/Vulkan.
2. A platform-keyed `switch` statement selects the renderer:

   ```cpp
   // Windows renderer map: 0=DX11, 1=DX12, 2=OpenGL, 3=Vulkan
   switch (rendererType) {
       case 0: renderer = std::make_shared<DX11Renderer>();   break;
       case 1: renderer = std::make_shared<DX12Renderer>();   break;
       case 2: renderer = std::make_shared<OpenGLRenderer>(); break;
       case 3: renderer = std::make_shared<VulkanRenderer>(); break;
   }
   ```

3. Before constructing a DX11 or DX12 renderer, a lightweight hardware support check is performed:
   - **`IsDX11Supported()`** — calls `D3D11CreateDevice()` with `D3D_DRIVER_TYPE_HARDWARE` and checks that the returned feature level is `D3D_FEATURE_LEVEL_11_0` or higher.
   - **`IsDX12Supported()`** — enumerates DXGI adapters via `IDXGIFactory4`, skips software adapters, and calls `D3D12CreateDevice()` with `D3D_FEATURE_LEVEL_12_0` to verify DX12 support.
4. If the hardware check fails, `ShowUnsupportedRendererMessage()` displays a `MessageBoxW` error dialog naming the unsupported API and the engine exits cleanly.
5. On success, `renderer` is populated and a `LOG_INFO` message is written via `debug.logLevelMessage()`.

#### Platform-renderer mapping

| Platform | Valid renderer indices |
| --- | --- |
| Windows | 0 = DX11, 1 = DX12, 2 = OpenGL, 3 = Vulkan |
| Linux / Android | 0 = OpenGL, 1 = Vulkan |
| macOS / iOS | 0 = OpenGL (only) |

The factory validates the `rendererType` value against the compiled-in renderer token. If the config requests DX11 but the build token is `__USE_VULKAN__`, the case for DX11 is absent from the `switch` (it is inside `#if defined(__USE_DIRECTX_11__)`), and the `default:` branch logs a critical error and exits. This prevents a DX12 binary from accidentally initialising an OpenGL renderer because the `rendererType` field was set to 2 in `GameConfig.cfg`.

[Back to Table of Contents](#table-of-contents)

---

## 7. Shader Management System

**Source files:** [ShaderManager.cpp](ShaderManager.cpp) / [ShaderManager.h](ShaderManager.h), [ShaderLoaders.cpp](ShaderLoaders.cpp)
**Documentation:** [ShaderManager-Example-Usage.md](ShaderManager-Example-Usage.md)

`ShaderManager` is the centralised shader lifecycle system for all four renderer backends. It handles loading HLSL or GLSL source files from disk, compiling them to backend-specific bytecode or GPU objects, linking multi-stage programs, caching compiled results, gracefully recovering from compilation failures, hot-reloading modified shaders in Debug builds, and providing per-shader reference counting for safe unloading. A companion file, `ShaderLoaders.cpp`, contains the global scene-specific shader registration routines.

### 7.1 Why a Dedicated Shader Manager Is Needed

Each renderer backend handles shader compilation in a fundamentally different way:

- **DirectX 11/12** — HLSL source is compiled to bytecode (`.cso`) either ahead-of-time by `fxc.exe` or at runtime by `D3DCompileFromFile()`. The bytecode is then loaded into typed GPU objects (`ID3D11VertexShader`, `ID3D11PixelShader`, etc.) and an input layout is created from the vertex shader reflection data.
- **OpenGL** — GLSL source is compiled at runtime by the driver via `glCompileShader()`. Individual shader objects are linked into a `GLuint` program object by `glLinkProgram()`.
- **Vulkan** — GLSL source is compiled to SPIR-V binary (`std::vector<uint32_t>`) at runtime via `shaderc`, then wrapped in a `VkShaderModule`. A separate pipeline compilation step references the module.

Without a managing layer, every subsystem (models, FX, GUI) would need to implement its own compilation, error handling, and caching — producing duplicated, fragile, backend-specific code. `ShaderManager` isolates all of this behind a name-based API: callers ask for a `ShaderProgram` by name and receive a compiled, linked, ready-to-bind object regardless of which backend is active.

### 7.2 Core Data Structures

#### ShaderType — Pipeline Stage Enumeration

```cpp
enum class ShaderType : int {
    VERTEX_SHADER = 0,
    PIXEL_SHADER,
    GEOMETRY_SHADER,
    HULL_SHADER,                    // DX11/12 tessellation hull stage
    DOMAIN_SHADER,                  // DX11/12 tessellation domain stage
    COMPUTE_SHADER,
    TESSELLATION_CONTROL_SHADER,    // OpenGL equivalent of hull shader
    TESSELLATION_EVALUATION_SHADER, // OpenGL equivalent of domain shader
    UNKNOWN_SHADER
};
```

#### ShaderPlatform — Target Backend

```cpp
enum class ShaderPlatform : int {
    PLATFORM_DIRECTX11 = 0,
    PLATFORM_DIRECTX12,
    PLATFORM_OPENGL,
    PLATFORM_VULKAN,
    PLATFORM_AUTO_DETECT    // Inferred from the active renderer token at runtime
};
```

`PLATFORM_AUTO_DETECT` is the default. `DetectPlatformFromRenderer()` resolves this at `Initialize()` time by reading the `renderer->RenderType` field, so callers never need to specify the platform explicitly.

#### ShaderProfile — Compilation Settings

```cpp
struct ShaderProfile {
    std::string entryPoint;         // e.g. "VSMain", "PSMain", "main"
    std::string profileVersion;     // e.g. "vs_5_0", "ps_5_0", "330 core"
    std::vector<std::string> defines; // Preprocessor definitions injected at compile time
    bool optimized;                 // Enable compiler optimisation (default: true)
    bool debugInfo;                 // Embed debug symbols in bytecode (default: false)
};
```

The profile is optional — if not supplied, `ShaderManager` applies sensible defaults for the active platform (e.g. `vs_5_0` / `ps_5_0` for DX11).

#### ShaderResource — Per-Shader Container

Each loaded shader is stored as a `ShaderResource` in the `m_shaders` map. The structure holds:

- **Identity** — `name` (unique string key), `filePath`, `type`, `platform`, `profile`.
- **Backend-specific GPU handles** (conditionally compiled):
  - DX11/12: `ComPtr<ID3D11VertexShader>`, `ComPtr<ID3D11PixelShader>`, `ComPtr<ID3D11GeometryShader>`, `ComPtr<ID3D11HullShader>`, `ComPtr<ID3D11DomainShader>`, `ComPtr<ID3D11ComputeShader>`, `ComPtr<ID3DBlob> shaderBlob`, `ComPtr<ID3D11InputLayout> inputLayout`.
  - OpenGL: `GLuint openglShaderID`, `GLuint openglProgramID`.
  - Vulkan: `VkShaderModule vulkanShaderModule`, `std::vector<uint32_t> spirvBytecode`.
- **Status flags** — `isCompiled`, `isLoaded`, `compilationErrors` (string).
- **Hot-reload support** — `lastModified` (`std::chrono::system_clock::time_point`) stores the file's modification timestamp; checked against the current timestamp on each hot-reload poll.
- **Reference management** — `referenceCount` (number of models/passes currently using this shader), `isInUse` flag.

#### ShaderProgram — Multi-Stage Combination

A `ShaderProgram` groups named shader stages into a single bindable unit:

```cpp
struct ShaderProgram {
    std::string programName;
    std::string vertexShaderName;
    std::string pixelShaderName;
    std::string geometryShaderName;   // optional
    std::string hullShaderName;       // optional (DX)
    std::string domainShaderName;     // optional (DX)
    std::string computeShaderName;    // optional
    bool isLinked;
    std::string linkingErrors;
#if defined(__USE_OPENGL__)
    GLuint openglProgramID;           // linked GL program object
#endif
};
```

On DX11/12 there is no explicit link step — individual shader objects are bound separately via the device context. On OpenGL, `glLinkProgram()` is called during `CreateShaderProgram()` and the result stored in `openglProgramID`.

#### ShaderManagerStats — Performance Counters

`ShaderManagerStats` tracks `totalShadersLoaded`, `totalProgramsLinked`, `compilationFailures`, `linkingFailures`, `memoryUsage` (bytes), and `lastActivity` timestamp. Retrieved via `GetStatistics()` and printed via `PrintDebugInfo()`.

### 7.3 Shader Loading and Compilation Pipeline

The full path from disk to GPU object for a single shader:

1. **`LoadShader(name, filePath, type, profile)`** is called.
2. **`ReadShaderFile(filePath, outContent)`** reads the raw source string via `FileIO`.
3. **`ParseShaderProfile(source, profile)`** scans for in-source annotations (optional — callers may supply the profile directly).
4. The source and profile are written into a new `ShaderResource` and stored in `m_shaders[name]`.
5. The method routes to the backend-specific compiler:
   - **`CompileHLSL(shader)`** → calls `D3DCompileFromFile()` (or loads a pre-built `.cso` if the file extension is `.cso`). On success, stores the compiled `ID3DBlob` and then calls the typed creation method (`CompileD3D11VertexShader`, `CompileD3D11PixelShader`, etc.) which calls `ID3D11Device::CreateVertexShader()` / `CreatePixelShader()` etc. For vertex shaders, `CreateInputLayoutForShader()` reflects the bytecode to build the `ID3D11InputLayout`.
   - **`CompileGLSL(shader)`** → calls `glShaderSource()` + `glCompileShader()`. Queries the compile status via `glGetShaderiv(GL_COMPILE_STATUS)`. On failure, retrieves the error log via `glGetShaderInfoLog()`.
   - **`CompileSPIRV(shader)`** → calls `shaderc::Compiler::CompileGlslToSpv()`. Stores the resulting `std::vector<uint32_t>` as `spirvBytecode`. Then calls `CreateVulkanShaderModule()` which wraps it in a `VkShaderModule` via `vkCreateShaderModule()`.
6. On any compilation error, `HandleCompilationError()` is called (see section 7.4).
7. `m_stats.totalShadersLoaded` is incremented on success.

**Loading from a string** (`LoadShaderFromString`) bypasses step 2 and feeds the provided source directly into step 3. Used for inline shaders (e.g. the error fallback shader itself).

### 7.4 Shader Program Linking

After individual stages are compiled, they are combined into a named `ShaderProgram` via `CreateShaderProgram(programName, vertexName, pixelName, ...)`:

- The method looks up each named `ShaderResource` from `m_shaders`.
- **DX11/12**: no explicit link step — the program simply records the constituent stage names. At bind time (`UseShaderProgram`), the device context receives `VSSetShader()`, `PSSetShader()`, `IASetInputLayout()` calls.
- **OpenGL**: `glCreateProgram()` → `glAttachShader()` for each stage → `glLinkProgram()`. Link errors are retrieved via `glGetProgramInfoLog()`. `DiagnoseShaderLinkageErrors()` provides additional context for common failures (unresolved uniforms, missing varyings).
- The linked `ShaderProgram` is stored in `m_programs[programName]`.

### 7.5 Error Handling — Colour-Coded Fallback Shader

If a shader fails to compile or a program fails to link:

1. `HandleCompilationError()` / `HandleLinkingError()` receives the error string.
2. The full error is routed to `debug.logLevelMessage(LOG_ERROR, ...)` — it appears in the debug output and the on-screen diagnostic log.
3. `m_stats.compilationFailures` is incremented.
4. A **colour-coded fallback shader** (solid magenta or another distinctive colour not used by correct content) is substituted for the failed shader, so:
   - The application keeps running — no crash.
   - The broken model or FX renders in an immediately obvious error colour.
   - The developer can read the error in the debug overlay without needing to attach a debugger.

The fallback shader is compiled from an inline source string via `LoadShaderFromString()` and reused for all failure cases in the current session.

### 7.6 Hot-Reloading (Debug Builds)

Hot-reloading allows shader source files to be edited on disk while the engine is running, with the changes taking effect without restarting:

1. `EnableHotReloading(true)` is called during `Initialize()` in Debug builds.
2. `CheckForShaderFileChanges()` is called periodically (from the loader thread or a timer callback). For each `ShaderResource` with `isCompiled = true`, it calls `GetFileModificationTime(shader.filePath)` and compares against `shader.lastModified`.
3. If the timestamp is newer, `ReloadShader(name)` is called: the existing GPU object is released, the file is re-read, and `CompileHLSL` / `CompileGLSL` / `CompileSPIRV` is invoked again.
4. On success, `UpdateShaderFileTimestamp(shader)` writes the new timestamp.
5. The shader lock (`AcquireShaderLock`) is held for the duration of the reload to prevent the render thread from binding a half-compiled object.

Hot-reloading is disabled in Release builds via `m_hotReloadingEnabled = false`.

### 7.7 Thread Safety

`ShaderManager` uses the engine's `ThreadManager` / `ThreadLockHelper` system:

- At `Initialize()`, a named lock is registered: `m_lockName` is unique to the shader manager instance.
- `AcquireShaderLock(timeoutMs = 2000)` acquires the lock via `ThreadManager` with a 2-second timeout. If the lock cannot be acquired within the timeout, a `LOG_WARNING` is emitted to flag a potential deadlock.
- `ReleaseShaderLock()` releases it.
- All public methods that read or write `m_shaders` or `m_programs` acquire the lock first. This allows the render thread to call `GetShader()` and the loader thread to call `ReloadShader()` concurrently without data races.

### 7.8 Integration with Engine Subsystems

| Method | What it does |
| --- | --- |
| `BindShaderToModel(programName, model)` | Associates a named program with a `Model` object so `model->Draw()` automatically binds the correct shaders |
| `SetupLightingShaders(lightManager)` | Populates the lighting uniform bindings in all loaded programs that reference the lighting constant buffer (`b1` / `b3`) |
| `LoadSceneShaders(sceneManager)` | Loads the shader set required for a specific `SceneType` as declared in `SceneShaderManager` |
| `UseShaderProgram(programName)` | Binds the named program to the active renderer pipeline; caches `m_currentProgramName` to skip redundant re-binds |
| `UnbindShaderProgram()` | Clears the bound program; used between render passes |

#### SceneShaderManager — Scene-to-Shader Mapping

`ShaderLoaders.cpp` defines a helper class `SceneShaderManager` that maps each `SceneType` to the list of shader names it requires:

```cpp
m_sceneShaders[SCENE_GAMEPLAY] = { "ModelVertex", "ModelPixel" };
```

`LoadSceneShaders(sceneManager)` reads the active scene type, looks up the required shader list, and calls `LoadShader()` for any that are not already in the cache.

### 7.9 Shader File Naming Convention

All shader source files live in `Assets/Shaders/`. The naming convention is:

| File | Registered Name | Stage |
| --- | --- | --- |
| `ModelVertex.hlsl` / `ModelVertex.glsl` | `"ModelVertex"` | Vertex shader — transforms geometry, outputs clip-space positions, normals, UVs, tangents |
| `ModelPixel.hlsl` / `ModelPixel.glsl` | `"ModelPixel"` | Pixel/fragment shader — PBR lighting, texture sampling, shadow attenuation |
| `DefaultVertex.hlsl` | `"DefaultVertex"` | Fallback vertex shader for un-textured geometry |
| `DefaultPixel.hlsl` | `"DefaultPixel"` | Fallback pixel shader — solid colour output |

The engine's master shader list in `Includes.h` declares the minimum required shaders for the model pipeline:

```cpp
const std::vector<std::string> MyShaders = { "ModelVertex", "ModelPixel" };
```

Additional shaders (fade effects, starfield, particles, GUI) are registered by their respective subsystems when those systems initialise.

### 7.10 Statistics and Diagnostics

- **`GetStatistics()`** returns a `ShaderManagerStats` snapshot: total loaded shaders, linked programs, compilation and linking failure counts, estimated GPU memory usage, and timestamp of last activity.
- **`PrintDebugInfo()`** formats the statistics as a human-readable block and routes it to the `Debug` system's console output.
- **`ValidateAllShaders()`** iterates every loaded `ShaderResource` and confirms it has a valid GPU handle (`isCompiled && isLoaded`). Any invalid entry is logged at `LOG_WARNING`. Useful as a diagnostic step after scene changes or renderer re-initialisation.

[Back to Table of Contents](#table-of-contents)

---

## 8. Camera System

**Source files:** `DXCamera.cpp/.h`, `VulkanCamera.cpp/.h`, `OpenGLCamera.cpp/.h`  
**Documentation:** [DXCamera-Example-Usage.md](DXCamera-Example-Usage.md)

The Camera system provides a full 3D camera with smooth animation, free-look, orbital, and history navigation capabilities.

### Purpose

A game camera must do more than just compute a view matrix — it must handle smooth movement between points, respond to player input, maintain target focus during movement, and integrate safely with the multi-threaded render pipeline. The CPGE Camera class handles all of this.

### Architecture

Each renderer backend has its own camera class (DXCamera, VulkanCamera, OpenGLCamera) that shares the same behavioural logic but produces view/projection matrices in the format expected by its respective API. The camera produces:
- A **view matrix** constructed from the camera position, target, and up vector using a left-handed look-at formulation.
- A **projection matrix** computed from the field-of-view (configured in `GameConfig.cfg`), aspect ratio, near plane, and far plane values.

Both matrices are uploaded to the renderer's constant buffer at `b0` every frame.

### Key Features

- **Smooth Jump Animation** — Bézier-curve-based positional movement with configurable interpolation speed. The camera arcs gracefully between positions rather than teleporting or sliding linearly.
- **History System** — A ring buffer of previous camera positions allows the player (or game code) to step backward through the camera's movement history.
- **Continuous Orbital Rotation** — The camera can orbit a target point at a configurable angular velocity for cinematic panning shots or automated demonstrations.
- **Pitch / Yaw / Roll** — Full rotation support with configurable min/max pitch clamp (`minPitch` / `maxPitch` in `GameConfig.cfg`) to prevent gimbal issues.
- **Joystick Integration** — The Camera class exposes a movement interface that the Joystick class drives directly for 3D navigation.
- **MathPrecalculation Integration** — Trigonometric operations inside the camera's movement routines are routed through the `MathPrecalculation` fast-lookup system (see [Section 23](#23-mathematics-precalculation-system)) for optimal frame-rate performance.
- **Thread Safety** — All camera state mutations are guarded by a mutex compatible with the `ThreadManager` / `ThreadLockHelper` system.

[Back to Table of Contents](#table-of-contents)

---

## 9. Scene Management System

**Source files:** `SceneManager.cpp/.h`  
**Documentation:** [SceneManager-Example-Usage.md](SceneManager-Example-Usage.md)

The `SceneManager` is the engine's high-level world organiser — it defines what is visible in the game world at any given moment by loading, caching, and managing scenes described by GLTF 2.0 files.

### Purpose

A scene in CPGE is a self-contained unit containing models, lights, cameras, and metadata. The SceneManager orchestrates the transition between scenes (with fade-out / fade-in sequences), maintains a binary cache of parsed geometry to avoid re-parsing the same GLTF on every startup, and provides the renderer with a frame-consistent snapshot of all objects to draw.

### GLTF 2.0 Parser

CPGE includes a **fully in-house GLTF 2.0 parser** supporting both `.gltf` (JSON + separate binary) and `.glb` (binary-embedded) files. The parser handles:

- **Mesh primitives** — including multi-primitive nodes (one node with several mesh sections, each with its own material). Each primitive past the first is given the parent model's ID and an identity TRS so that full-model animation moves all primitives in concert.
- **Materials** — PBR metallic-roughness, including base colour factor, metallic factor, roughness factor, emissive factor, alpha mode, and double-sided flag.
- **Textures and samplers** — including `KHR_texture_transform` extension support; UV transforms are baked into vertex UV coordinates at parse time so no special shader variant is needed at runtime.
- **Lights** — `KHR_lights_punctual` extension for point, directional, and spot lights embedded in the scene file.
- **Cameras** — GLTF-defined perspective cameras are imported and available as named camera objects.
- **Animations** — keyframe animation data (translation, rotation, scale per node) is extracted and handed off to the `GLTFAnimator` (see [Section 11](#11-gltf-animation-system)).
- **FBX Import** — `FBXImport.cpp/.h` provides an additional in-house FBX format parser so that assets exported directly from tools like Maya or 3ds Max can be loaded alongside GLTF content.

### Geometry Cache (`cache.dat`)

After a scene's GLTF file is parsed for the first time, the resulting geometry (vertex buffers, index buffers, material data) is serialised to a binary `cache.dat` file in the working directory. On subsequent runs, `cache.dat` is read at startup and the geometry is reconstructed directly — bypassing the JSON parse step entirely. This can reduce load times significantly for scenes with many large meshes.

**Important:** When models are restored from `cache.dat`, the material/texture binding step that normally happens inside the full GLTF parse loop must be explicitly re-executed. The engine's `SceneManager` contains dedicated re-bind logic (`RefreshDX12Textures()` on DX12, `RefreshOpenGLTextures()` on OpenGL) that is called immediately after cache restoration to prevent grey-geometry rendering.

### Scene Transition

Scene transitions follow a fixed sequence to prevent use-after-free on GPU resources:
1. Begin fade-out (FXManager `AddColorFadeEffect`).
2. Await fade completion (drain `bIsRendering` flag on all active FX).
3. Call `StopAllFX()` to cleanly terminate active effects.
4. Unload current scene geometry and GPU resources.
5. Load next scene and restore FX state via `RestoreFXAfterScene()`.
6. Begin fade-in.

[Back to Table of Contents](#table-of-contents)

---

## 10. 3D Model System

**Source files:** `Models.cpp/.h`, `DX12Models.cpp/.h`, `OpenGLModels.cpp/.h`, `VulkanModels.cpp/.h`  
**Documentation:** [Model-Example-Usage.md](Model-Example-Usage.md)

The Model system manages individual 3D geometry objects — from their raw vertex/index data through GPU buffer upload, material binding, texture loading, and per-frame constant buffer updates.

### Purpose

Every rendered 3D entity in CPGE (whether loaded from GLTF, OBJ, or FBX) is represented as a `ModelInfo` structure (or a class wrapping it, depending on the backend). The Model subsystem owns the lifecycle of these objects from load-time through destruction.

### ModelInfo Structure

`ModelInfo` is the central data record for a 3D model. It contains:

- **Geometry** — vertex buffer, index buffer, vertex count, index count, and primitive topology.
- **Transform** — world matrix (position, rotation, scale) stored as a row-vector `Matrix4x4`, uploaded raw (without an implicit transpose) to the `std140` UBO in OpenGL/Vulkan, and directly to the DX11 constant buffer.
- **Material** — base colour RGBA, metallic factor, roughness factor, emissive factor, ambient occlusion scale, and flags for double-sided rendering, alpha blending, and emissive emission.
- **Textures** — handles (or IDs) for each of the 9 texture slots defined in `Includes.h`.
- **UV wrap modes** — `uvWrapU` / `uvWrapV` per model, applied as sampler parameters on each backend.
- **Animation state** — a pointer to the `GLTFAnimator` instance and the current keyframe time.
- **Rendering flags** — visible, cast shadow, receive shadow, instance index.
- **Parent model ID** — for multi-primitive GLTF nodes, all child primitives reference the first primitive's ID so that full-model transforms propagate correctly.

### Instanced Rendering

Each model instance maintains its own copy of the `ModelInfo` structure, isolated from the base asset. This means two ships loaded from the same `.gltf` file share vertex and index buffers on the GPU (read-only) but have independent world matrices, material overrides, and animation states. The base asset is never mutated by instance operations.

### Wavefront OBJ Support

In addition to GLTF, the engine includes a built-in `.obj` / `.mtl` parser for simpler geometry needs. OBJ files are parsed into the same `ModelInfo` record and follow the identical lifecycle.

### Backend-Specific Upload

- **DX11** — vertex and index buffers are created as immutable `D3D11_USAGE_DEFAULT` resources, uploaded via `ID3D11DeviceContext::UpdateSubresource` at load time.
- **DX12** — vertex and index buffers are uploaded through an intermediate upload heap (`D3D12_HEAP_TYPE_UPLOAD`) and then copied to a default heap (`D3D12_HEAP_TYPE_DEFAULT`) via `CopyBufferRegion` on the copy command queue.
- **OpenGL** — buffers are created as VAO / VBO / EBO objects. The OpenGL models layer lives exclusively in `OpenGLModels.cpp` (not in the shared `Models.cpp`) to keep backend-specific GL calls cleanly separated.
- **Vulkan** — `VkBuffer` objects for vertex and index data, backed by `VkDeviceMemory` with `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`.

[Back to Table of Contents](#table-of-contents)

---

## 11. GLTF Animation System

**Source files:** `GLTFAnimator.cpp/.h`

The `GLTFAnimator` class interprets the keyframe animation data extracted by the SceneManager's GLTF parser and applies it to models every frame.

### Purpose

GLTF 2.0 animations are defined as a set of channels, each targeting a specific node property (translation, rotation, or scale) and referencing an accessor that provides an array of time-stamped keyframe values. The `GLTFAnimator` consumes this data and produces a world matrix for each animated node at any given time `t`.

### Interpolation Modes

CPGE supports all three GLTF-standard interpolation modes:

| Mode | Behaviour |
| --- | --- |
| `LINEAR` | Values are linearly interpolated between adjacent keyframes (LERP for translation/scale, SLERP for quaternion rotation). |
| `STEP` | Value holds constant at the previous keyframe until the next keyframe is reached — produces snapping transitions. |
| `CUBICSPLINE` | Uses GLTF cubic-spline tangent data for smooth, natural motion curves (Hermite interpolation). |

### Multi-Primitive Node Handling

When a GLTF node contains multiple mesh primitives, the first primitive receives the node's full TRS from the animation system. All subsequent primitives carry `iParentModelID = firstPrimIndex` and `gltfNodeIndex = -1` with an identity TRS, so the animation system recognises them as passengers and applies the parent's animated matrix rather than computing an independent transform for each.

### Matrix Convention

Animation matrices are computed in GLTF's right-handed Y-up coordinate system and then transformed to the engine's left-handed Y-up system at the point of upload to the GPU constant buffer. The axis swap is applied once at the boundary — not baked into the stored keyframe data — to keep the GLTF data canonical and re-usable.

### Ship Orientation

The GLTF exporter in Blender outputs ship (and other directional) models facing the correct direction when the export settings match the engine's expected orientation. No post-load rotation fixup is required for models authored with the standard Blender GLTF export settings.

[Back to Table of Contents](#table-of-contents)

---

## 12. Lighting System

**Source files:** `Lights.cpp/.h`  
**Documentation:** [Light-Example-Usage.md](Light-Example-Usage.md)

The `LightsManager` class manages a collection of `Light` objects and serialises them to the GPU light buffer bound at constant buffer slot `b1` (per-model) and `b3` (global directional).

### Purpose

Real-time lighting requires both CPU-side management (creating, animating, and culling lights) and GPU-side packing (aligning light data to constant-buffer layout rules and uploading it before each draw call). The Lights system handles both sides.

### Light Types

| Type | Description |
| --- | --- |
| **Directional** | Infinite-distance light with a direction vector; models the sun or moon. No position, no attenuation. |
| **Point** | Omnidirectional light at a world-space position; attenuates with distance using a constant/linear/quadratic falloff model. |
| **Spot** | Directed cone of light at a position; defined by a direction vector and inner/outer cone angles. |

### LightStruct (GPU Layout)

Each light is packed into a `LightStruct` that respects the `std140` alignment rules required by OpenGL UBOs and the explicit packing rules for DX11/DX12 constant buffers. Fields include:
- Position (XMFLOAT3 + padding)
- Direction (XMFLOAT3 + padding)
- Colour (XMFLOAT4 — RGB + intensity)
- Attenuation constants (constant, linear, quadratic, range)
- Spot cone angles (inner/outer cosines)
- Type flag and enabled flag.

### Light Animation

The `LightsManager` supports three built-in light animation modes:

| Mode | Effect |
| --- | --- |
| **Flicker** | Random-period intensity oscillation simulating a candle or failing lamp. |
| **Pulse** | Sinusoidal intensity sweep for a breathing or heartbeat light effect. |
| **Strobe** | Square-wave on/off toggling at a configurable frequency. |

Animation is advanced each frame by the `LightsManager::Update(deltaTime)` call in the render loop.

### GLTF Integration

Lights defined in GLTF files via the `KHR_lights_punctual` extension are automatically imported by the SceneManager and registered with the `LightsManager` under their scene names. They can be manipulated at runtime by name after the scene loads.

[Back to Table of Contents](#table-of-contents)

---

## 13. Material & Texture System (PBR)

The engine implements a **Physically Based Rendering (PBR)** material model following the **metallic-roughness workflow** defined in the GLTF 2.0 specification.

### PBR Material Properties

| Property | Description | Default |
| --- | --- | --- |
| `baseColorFactor` | RGBA multiplier applied to the albedo texture | `(1, 1, 1, 1)` |
| `metallicFactor` | 0 = dielectric, 1 = full metal | `1.0` |
| `roughnessFactor` | 0 = mirror-smooth, 1 = fully diffuse | `1.0` |
| `emissiveFactor` | RGB multiplier for the emissive map | `(0, 0, 0)` |
| `alphaCutoff` | Discard threshold for `MASK` alpha mode | `0.5` |
| `doubleSided` | Whether backfaces are rendered | `false` |

### UV Transform Support

The `KHR_texture_transform` GLTF extension is fully supported. UV offset, rotation, and scale transforms read from the GLTF material are **baked into the vertex UV coordinates at parse time**. This means the vertex data already has the final UV values after loading — no shader variant or additional uniform is needed at runtime.

For FBX assets, UV transforms are similarly baked into vertex data during FBX import.

### Wrap Mode Control

Each model instance exposes `uvWrapU` and `uvWrapV` fields (values: `WRAP`, `MIRROR`, `CLAMP`). These are applied as sampler object parameters when the model's material is bound for rendering. On DX12, a static `WRAP` sampler is baked into the root signature and is not configurable at the instance level (this is a known limitation that will be addressed in a future update).

[Back to Table of Contents](#table-of-contents)

---

## 14. Visual FX Manager

**Source files:** `DX_FXManager.cpp/.h`, `DX12FXManager.cpp/.h`, `VULKAN_FXManager.cpp/.h`, `OpenGLFXManager.cpp/.h`  
**Documentation:** [FXManager-Example-Usage.md](FXManager-Example-Usage.md)

The `FXManager` (and its backend variants `DX12FXManager`, `VKFXManager`, `GLFXManager`) is a comprehensive visual effects engine that manages a queue of simultaneous real-time screen effects.

### Purpose

Visual transitions and effects are a fundamental part of any polished game — fade-ins, explosions, starfields, scrolling backgrounds, text scrollers, and loading overlays all need to run simultaneously, be queued in sequence, and be safely started/stopped from multiple threads. The FXManager handles all of this with a uniform API regardless of the active renderer.

### Effect Types

| Effect | Description |
| --- | --- |
| **Color Fade** | Full-screen fade in/out to any RGBA colour. Used for scene transitions and death/respawn sequences. |
| **Scroll Effect** | Horizontally or vertically scrolling background image at configurable pixel speeds. |
| **Particle Explosion** | CPU-simulated burst of coloured point particles with velocity, gravity, and lifetime. |
| **Starfield** | 2D parallax star layer system simulating depth-of-field in space environments. |
| **Warp Dot Tunnel** | Animated 3D warp-tunnel effect built from procedurally-placed colour dots converging on a vanishing point. Used for hyperspace / warp-speed transitions. |
| **Text Scroller** | Horizontally or vertically scrolling text string with configurable font, speed, and colour — used for credit rolls and news-ticker displays. |
| **Text Fade In/Out** | Smooth opacity transition for overlay text strings (loading messages, cutscene subtitles). The fade progress is tracked separately from timeout so effects that complete their fade are removed on `progress >= 1.0` rather than on a timer. |

### Queue-Based Architecture

Effects are submitted to an internal priority queue. Each effect record carries:
- Effect type and parameters.
- Duration, start time, and current progress (`0.0` to `1.0`).
- An optional **callback function** (`std::function<void()>`) that fires when the effect completes — used for chaining effects or triggering scene loads.
- A **bIsRendering** atomic flag that prevents the effect from being mutated or removed while the GPU is still drawing it.

Multiple effects can be active simultaneously (e.g., a starfield runs continuously underneath a fade that triggers once).

### Scene-Transition Safety

The correct scene-transition ordering is: fire fade first → call `StopAllFX()` → wait for `bIsRendering` to drain → mutate scene → call `RestoreFXAfterScene()`. Mutating the effects queue while any effect still has `bIsRendering = true` causes a use-after-free on the GPU-side vertex data for that effect.

### Backend Parity

| Backend | Class | Status |
| --- | --- | --- |
| DirectX 11 | `FXManager` | Complete — all effects supported |
| DirectX 12 | `DX12FXManager` | Complete — all effects supported |
| Vulkan | `VKFXManager` | Complete — all effects supported |
| OpenGL | `GLFXManager` | Complete — all effects supported |

All four implementations expose identical public APIs; game code addresses the correct global instance (`fxManager`) without needing to branch on the renderer.

### Performance

All trigonometric operations used by the warp tunnel and particle effects are routed through the `MathPrecalculation` lookup table system to avoid `sinf`/`cosf` calls in per-particle update loops.

[Back to Table of Contents](#table-of-contents)

---

## 15. Threading System

**Source files:** `ThreadManager.cpp/.h`, `ThreadLockHelper.h`  
**Documentation:** [ThreadManager-Example-Usage.md](ThreadManager-Example-Usage.md)

The `ThreadManager` class provides a complete, named-thread lifecycle management system for all of CPGE's concurrent operations.

### Purpose

A game engine must carefully manage several concurrent execution contexts — the render thread, the asset loader thread, the audio playback thread, the network thread, and the FX update thread. Without a structured system, creating, tracking, and cleanly shutting down these threads is error-prone. `ThreadManager` provides that structure.

### Named Thread Identification

All threads in CPGE are identified by a `ThreadNameID` enum value rather than raw OS thread IDs. This allows log messages, status queries, and shutdown signals to reference threads by logical name rather than opaque integers. Registered names include (among others):
- `THREAD_RENDERER` — the primary render loop.
- `THREAD_LOADER` — the IO asset loader thread.
- `THREAD_AUDIO` — the XMMODPlayer / SoundManager audio mixing thread.
- `THREAD_NETWORK` — the NetworkManager receive/send loop.
- `THREAD_AI` — the GamingAI analysis thread.
- `THREAD_FILEIO` — the FileIO queued operation thread.

### Thread Control Operations

`ThreadManager` exposes:
- **Start / Stop / Pause / Resume** — lifecycle control for any named thread.
- **IsRunning / IsPaused** — real-time status queries safe to call from any thread.
- **WaitForThread** — blocking wait with optional timeout for orderly shutdown sequencing.
- **Shared Variables** — a thread-safe key-value store for inter-thread communication without raw shared memory.

### Lock Management

All locking in the engine uses `ThreadManager`-created named `std::mutex` objects. The `ThreadLockHelper` class provides **RAII-style lock acquisition and release**:

```cpp
// Lock is acquired on construction, released on destruction (even if an exception fires).
ThreadLockHelper lock(threadManager.GetLock("SceneManager"));
// ... protected operations ...
// Lock is automatically released here when lock goes out of scope.
```

The `ThreadLockHelper` also supports **multiple-lock acquisition** with deadlock-avoidance ordering (always acquire locks in the same registered order), and **timeout-based acquisition** to detect potential deadlocks and report them to the Debug system.

### Graceful Shutdown

During application exit, `ThreadManager` sends a stop signal to all registered threads in dependency order (audio first, then loader, then render), waits for each to acknowledge, and then releases resources. This prevents crashes from renderer destruction racing against an audio callback or loader completing a GPU buffer upload.

[Back to Table of Contents](#table-of-contents)

---

## 16. IO Loader Thread

**Source files:** `IOLoaderThread.cpp/.h`

The `IOLoaderThread` is CPGE's dedicated background asset loader — it keeps the render thread free to maintain 60 fps while potentially large model, texture, and audio assets load from disk in parallel.

### Purpose

Synchronous asset loading on the render thread stalls the frame pipeline and produces visible hitches. The `IOLoaderThread` moves all disk-bound work off the render thread, signalling completion through atomic flags and condition variables so the render thread can proceed with a minimal stub (e.g., a loading-spinner FX) until the real content is ready.

### Operation Model

1. Game code submits a load request (file path + callback) to the `IOLoaderThread` queue.
2. The loader thread picks up the request, performs the file read (possibly decompressing via `PUNPack`), parses the asset, and uploads it to GPU memory.
3. On completion, an atomic completion flag is set and the optional callback is invoked — safely dispatching back to the render thread's command queue if a GPU operation is required.
4. The render thread polls the completion flag each frame (zero cost if not set) and swaps the stub resource for the final loaded resource.

This model ensures that neither the render thread nor the loader thread ever blocks waiting on the other.

### Thread Safety

The `IOLoaderThread` uses `ThreadManager`-managed locks and is registered as `THREAD_LOADER`. All GPU buffer creation calls that must happen on the render thread (DX12 command list recording, Vulkan command submission) are marshalled through a cross-thread command queue rather than called directly from the loader thread.

[Back to Table of Contents](#table-of-contents)

---

## 17. Sound Effects Manager (SoundManager)

**Source files:** `SoundManager.cpp/.h`  
**Documentation:** [SoundManager-Example-Usage.md](SoundManager-Example-Usage.md)

The `SoundManager` class provides a full-featured, priority-queued sound effects system built on **DirectSound** (Windows), with planned expansion to other platforms.

### Purpose

Sound effects in a game need priority management (a gunshot should not be delayed waiting for a quieter ambient sound to finish), cooldown control (prevent the same SFX from being triggered too rapidly), stereo positioning, and smooth fade transitions. The `SoundManager` handles all of this through an asynchronous worker thread so that SFX playback never blocks the render loop.

### Key Design Decisions

DirectSound was chosen over XAudio2 as the primary audio output layer after XAudio2 exhibited reliability issues in multi-threaded scenarios during development. DirectSound's simpler explicit buffer model provides more predictable behaviour and easier debugging at the cost of lacking some of XAudio2's higher-level DSP features.

### Priority Queue System

Each sound-play request carries a priority integer. The `SoundManager` maintains an internal `std::priority_queue` of pending requests. When multiple sounds compete for available mixing channels, higher-priority sounds pre-empt lower-priority ones. Sounds with equal priority are serviced in submission order.

### Fade-In Effects

The `SoundManager` supports smooth **fade-in on start** — the initial volume ramps from 0 to the requested level over a configurable duration. This prevents audio pops when a loop is activated and is also used for crossfade-style transitions between ambient layers.

### Cooldown System

Each sound definition has an optional cooldown period. If the same sound is requested within its cooldown window (e.g., a footstep SFX triggered too rapidly), the request is silently dropped. This prevents audio spam without requiring game code to track playback timing manually.

### Stereo Positioning

The `SoundManager` supports **left / right / centre** stereo balance via DirectSound's pan control. Full 3D positional audio (HRTF) is planned for a future release. Audio physics (Doppler effect, distance attenuation) is defined in the `Physics` class (Section 22) and feeds into stereo balance calculations.

### Threading Model

All DirectSound buffer operations (create, fill, play, stop, release) run on the `THREAD_AUDIO` worker thread managed by `ThreadManager`. Game code submits play requests to a lock-free queue; the audio thread drains the queue each tick.

[Back to Table of Contents](#table-of-contents)

---

## 18. Music Playback — XMMODPlayer

**Source files:** `XMMODPlayer.cpp/.h`  
**Documentation:** [XMMODPlayer-Example-Usage.md](XMMODPlayer-Example-Usage.md)

The `XMMODPlayer` class is a completely in-house implementation of an **Extended Module (XM)** tracker file player — one of the most distinctive subsystems in CPGE.

### Purpose

Rather than using MP3 or OGG files that stream fixed compressed audio, CPGE offers an XM tracker player. XM files encode music as a set of **patterns** (sequences of note events) played across up to 32 channels, with samples (short PCM clips of instruments) referenced by note. This means:
- An XM file containing multiple full musical pieces is still far smaller in storage than equivalent MP3 files.
- Music can transition smoothly between patterns mid-playback (e.g., jump from an intro pattern to a looping verse at the exact beat boundary), enabling adaptive, interactive music without cross-fades.
- Multiple songs can live in a single XM file as separate pattern sequences, accessible by pattern index.

### XM Format Support

The player implements the full Fast Tracker 2 XM specification, including:

- **Multi-channel mixing** — up to 32 simultaneous audio channels mixed in software to a stereo output stream.
- **Volume / panning envelopes** — smooth per-note volume and pan curves defined in the XM instrument data.
- **Effects** — the complete XM effect table: vibrato, tremolo, portamento (tone slide), arpeggio, volume slide, note delay, note cut, sample offset, and many more.
- **Bidirectional and ping-pong looping** — instrument samples loop correctly according to their XM loop flags.
- **Pattern jump commands** — the engine's XM player exposes direct pattern-sequence navigation APIs, allowing game code to queue a jump to a specific pattern number. This is the mechanism behind the **structured playlist** system (described in ABOUT.md) where multiple songs coexist within a single XM module file.

### Audio Output

Mixed audio is output at **44.1 kHz stereo** via DirectSound on Windows. The mixing callback runs on the dedicated `THREAD_AUDIO` worker thread so playback is never interrupted by render hiccups.

### Volume and Fade Controls

The player supports:
- **Global volume** — a master mixing gain applied to all channels simultaneously.
- **Per-channel volume** — independent gain per XM channel for fine-grained audio balancing.
- **Fade-in / fade-out** — smooth exponential volume ramps with configurable duration.
- **Hard pause / resume** — immediate silence (mutes all channels instantly, preserving playback position for a clean resume).

### Playlist Configuration

The active XM file list is defined in `Includes.h`:

```cpp
inline const std::filesystem::path xmFilePlaylist[] = { L"thevoid.xm", L"electro2.xm", L"battle.xm" };
inline const std::filesystem::path IntroXMFilename  = "thevoid.xm";
```

The global volume cap is also defined there: `const int MAX_GLOBAL_VOLUME = 64`.

[Back to Table of Contents](#table-of-contents)

---

## 19. Media Player — WinMediaPlayer

**Source files:** `WinMediaPlayer.cpp/.h`, `MoviePlayer.cpp/.h`

The `WinMediaPlayer` class provides **MP3 and M4A** music streaming via Windows Media Foundation, and `MoviePlayer` provides **in-game video playback** (cutscenes, intro movies) using the same Media Foundation pipeline.

### Purpose

While the XMMODPlayer handles the majority of in-game music needs, there are scenarios where full MP3/M4A streaming is preferred (e.g., licensed music tracks, pre-composed cinematic music). The `WinMediaPlayer` handles this use case. `MoviePlayer` extends this into video, decoding H.264 / VC-1 video streams from MP4 / WMV containers and rendering each decoded frame as a texture on a fullscreen quad.

### Media Foundation Architecture

Both systems use:
- `MFCreateSourceReaderFromURL` to open media files.
- `IMFSourceReader` to pull compressed samples.
- The Media Foundation transform pipeline to decode to PCM audio / raw video frames.
- For video: decoded BGRA frames are uploaded to a staging texture each frame and then copied to a GPU shader resource view.

Per-frame COM allocations that were previously performed inside `DrawVideoFrame` (a significant source of shutdown-time crashes) have been hoisted to class-level member `ComPtr<>` objects and are explicitly reset in `Cleanup()` and `Resize()` calls, eliminating both the per-frame heap cost and the race-condition crashes on clean exit.

[Back to Table of Contents](#table-of-contents)

---

## 20. Text-to-Speech Manager (TTSManager)

**Source files:** `TTSManager.cpp/.h`  
**Documentation:** [TTSManager-Example-Usage.md](TTSManager-Example-Usage.md)

The `TTSManager` provides **Text-to-Speech (TTS)** output for in-game dialogue, accessibility features, or developer diagnostic read-back.

### Purpose

TTS allows in-game events, NPC dialogue lines, tutorial instructions, or accessibility narration to be spoken aloud without requiring pre-recorded audio assets for every string. It also provides a valuable debugging tool — hearing a diagnostic message spoken aloud is sometimes faster than reading log output during active gameplay.

### Capabilities

- **Multiple simultaneous channels** — independent TTS streams can operate concurrently (e.g., a game event voice and a background ambient narration).
- **Voice selection** — where the host OS provides multiple installed TTS voices, the engine can select and configure them individually.
- **Volume control** — TTS volume is independently configurable via `TTSVolume` in `GameConfig.cfg`.
- **Graceful degradation** — if TTS initialisation fails (e.g., no TTS engine installed), the engine logs a warning and continues operating without TTS rather than treating it as a fatal error.

The `UseTTS` flag in `GameConfig.cfg` allows TTS to be globally enabled or disabled at runtime without a rebuild.

[Back to Table of Contents](#table-of-contents)

---

## 21. Input Systems

### 21.1 Keyboard Handler

**Source files:** `KeyboardHandler.cpp/.h`, `KBHandlersCode.cpp`  
**Documentation:** [KeyboardHandler-Example-Usage.md](KeyboardHandler-Example-Usage.md)

The `KeyboardHandler` is a **cross-platform singleton** keyboard input system designed for real-time game loops.

#### Purpose

Reading keyboard input in a game requires more than just checking `GetAsyncKeyState()` — it requires debouncing, key-combination detection, hotkey registration with callbacks, and AI-integration key logging. The `KeyboardHandler` provides all of this through a thread-safe, minimal-latency interface.

#### Key Features

- **Lock-free key state queries** — the hot path (`IsKeyDown`, `IsKeyUp`, `WasKeyPressed`) uses atomics rather than mutexes to avoid blocking the render loop at query time.
- **Key combination detection** — up to a configurable maximum of simultaneous keys are tracked; combinations such as `Ctrl+Alt+S` are detected through a set intersection check at query time.
- **Hotkey callback registration** — game code registers a key combination and a `std::function<void()>` callback; the handler invokes it on the next game loop tick when the combination is detected.
- **Key logging for AI** — the handler optionally records key presses with timestamps to a structured log consumed by the `GamingAI` subsystem for player behaviour analysis.
- **Platform support** — Windows uses `GetAsyncKeyState` / `WM_KEYDOWN` / `WM_KEYUP`; Linux uses X11 events; macOS uses Cocoa event routing; Android/iOS use touch-mapped virtual key events.

#### KBHandlersCode.cpp

`KBHandlersCode.cpp` contains the engine's own default keyboard handling logic — the standard response functions for keys like `Escape`, `F1`–`F12`, `Home` (screen recorder toggle), and debug overlays. Game-specific keys are registered on top of these defaults.

### 21.2 Joystick & GamePad System

**Source files:** `Joystick.cpp/.h`  
**Documentation:** [Joystick-Example-Usage.md](Joystick-Example-Usage.md)

The `Joystick` class provides Windows gamepad and joystick input using the DirectInput / WinMM joystick API (current), with XInput support planned.

#### Purpose

Both 2D (menu navigation, scrolling shooters) and 3D (flight sims, adventure games) game styles need gamepad input. The `Joystick` class provides a unified interface for both movement paradigms with configurable sensitivity and deadzone.

#### Key Features

- **Auto-detection** — up to 2 connected joysticks are detected automatically at startup; the configuration is refreshed on reconnect.
- **2D Mode** — X/Y axes map to a 2D movement vector; button mappings drive game actions (fire, jump, pause, etc.).
- **3D Mode** — X/Y axes drive camera strafing/forward motion; Z axis and rotational axes drive camera pitch/yaw. Integration is direct with the Camera class — the Camera exposes `Move3D(dx, dy, dz)` and `Rotate(pitch, yaw)` which the Joystick calls each frame.
- **Deadzone handling** — configurable deadzone radius prevents axis drift from worn hardware.
- **Sensitivity** — configurable via `joystickSensitivity` and `joystickRotationSensitivity` in `GameConfig.cfg`.
- **Custom button mappings** — per-player button remapping is serialised to JSON files and reloaded on startup.
- **Multi-player** — up to `MAX_PLAYERS` (configurable in `Includes.h`, default 1) players are supported, each mapped to an independent `Joystick` instance.

[Back to Table of Contents](#table-of-contents)

---

## 22. Physics Engine

**Source files:** `Physics.cpp/.h`  
**Documentation:** [Physics-Example-Usage.md](Physics-Example-Usage.md)

The `Physics` class is a comprehensive, self-contained physics simulation system covering rigid body motion, collision detection and response, ragdoll articulation, projectile mechanics, particle physics, and audio physics.

### Purpose

Physics in CPGE is not an external engine (no Bullet, no PhysX, no Havok). It is a purpose-built, domain-specific solver designed to run efficiently within the engine's frame budget without external library constraints. The goal is correctness in the scenarios CPGE games actually encounter — not a general-purpose rigid-body simulator for every conceivable scenario.

### Architecture

```text
Physics System
├── Integration (Euler, Verlet, Runge-Kutta 4)
├── Collision Detection
│   ├── Broad Phase  (Spatial Hashing)
│   ├── Narrow Phase (AABB, Sphere, OBB, Convex Hull)
│   └── Continuous   (Swept volume, time-of-impact)
├── Collision Response
│   ├── Impulse resolution
│   ├── Restitution (bounciness coefficient)
│   └── Friction (Coulomb model)
├── Gravity Fields (multiple, variable, distance-attenuated)
├── Ragdoll System (joint constraints, constraint solving)
├── Projectile / Newtonian Motion
├── Particle System Physics (wind, drag, gravity)
├── Audio Physics (Doppler, 3D propagation, occlusion)
└── Path Calculation (Bezier curves, splines, 2D/3D)
```

### Key Capabilities

- **Curved Path Calculations (2D and 3D)** — up to 1024 coordinate spline paths for smooth entity movement along pre-defined routes (enemy patrol paths, cutscene camera tracks, missile trajectories).
- **Reflection Physics** — accurate surface reflection vectors with configurable restitution (bounciness) and friction coefficients, used for projectile ricochets and ball-style physics.
- **Variable Gravity System** — multiple gravity fields can coexist in the scene, each with a direction, magnitude, and distance-based falloff. Entities experience the combined gravity from all active fields.
- **Ragdoll System** — joint-based articulated bodies with constraint solving for death animations, physics-driven character knockback, and debris scattering.
- **Newtonian Motion** — full F = ma integration with mass, velocity, acceleration, drag, and impulse forces. Projectile calculations include launch angle, gravity integration, and time-of-flight estimation.
- **Audio Physics** — 3D sound propagation using distance/angle-to-listener calculations, Doppler-effect pitch shifting for moving sound sources, and basic occlusion (wall/obstacle attenuation) feeding into the SoundManager's stereo balance control.
- **Particle System Physics** — each particle in an explosion or environmental effect receives an initial velocity, a gravity vector, a drag coefficient, and a wind force, with per-particle Euler integration run every physics tick.

### MathPrecalculation Integration

All trigonometric operations inside the physics update loops (`sin`, `cos`, `atan2`, `sqrt`) are routed through the `MathPrecalculation` lookup table system for maximum throughput.

[Back to Table of Contents](#table-of-contents)

---

## 23. Mathematics Precalculation System

**Source files:** `MathPrecalculation.cpp/.h`  
**Documentation:** [MathPrecalculation-Example-Usage.md](MathPrecalculation-Example-Usage.md)

The `MathPrecalculation` class provides a suite of **pre-computed lookup tables and fast-path macros** for the trigonometric and interpolation functions that appear most frequently in real-time game loops.

### Purpose

In a frame budget of ~16.7 ms (60 fps), calling `sinf` / `cosf` hundreds of times per frame (in particle systems, physics updates, and FX animations) measurably impacts performance on lower-end hardware. The `MathPrecalculation` system pre-computes these values at startup and replaces hot-path calls with table lookups — trading a small amount of accuracy (irrelevant at game-graphics precision) for a significant throughput improvement.

### Fast-Path Macros

| Macro | Equivalent |
| --- | --- |
| `FAST_SIN(x)` | `sinf(x)` via lookup table |
| `FAST_COS(x)` | `cosf(x)` via lookup table |
| `FAST_TAN(x)` | `tanf(x)` via lookup table |
| `FAST_ASIN(x)` | `asinf(x)` via lookup table |
| `FAST_ACOS(x)` | `acosf(x)` via lookup table |
| `FAST_ATAN2(y,x)` | `atan2f(y,x)` via lookup table |

### Colour Conversion

The system also provides:
- **RGB ↔ YUV** conversions for video processing (used by `MoviePlayer`).
- **Gamma correction** for physically-correct colour display.
- **Float colour conversions** for shader constant buffer packing.

### Interpolation & Easing

All standard interpolation types are provided:

| Function | Description |
| --- | --- |
| `Lerp(a, b, t)` | Linear interpolation |
| `SmoothStep(t)` | Hermite smoothstep (3t² - 2t³) |
| `SmootherStep(t)` | Ken Perlin's smoother step (6t⁵ - 15t⁴ + 10t³) |
| `EaseIn(t)` | Quadratic ease-in curve |
| `EaseOut(t)` | Quadratic ease-out curve |
| `EaseInOut(t)` | Symmetric quadratic ease |

These are used extensively by the Camera (Section 8), FXManager (Section 14), and Animation (Section 11) systems.

### Matrix Transformations

The `MathPrecalculation` class also provides helper functions for building common 3D transformation matrices — scale, rotation (X/Y/Z axes), translation, and combined TRS — used by the Model and Animation systems.

[Back to Table of Contents](#table-of-contents)

---

## 24. Networking Manager

**Source files:** `NetworkManager.cpp/.h`  
**Documentation:** [NetworkManager-Example-Usage.md](NetworkManager-Example-Usage.md)

The `NetworkManager` provides **TCP and UDP networking** for multiplayer game communication, server connections, and inter-process messaging.

### Purpose

Multiplayer games require reliable, low-latency network communication with session management, authentication, and extensible command dispatch. The `NetworkManager` provides this without relying on any third-party networking framework — everything from socket creation to packet framing and checksum validation is implemented in-house.

### Dual Protocol Support

| Protocol | Use Case |
| --- | --- |
| **TCP** | Reliable, ordered delivery — login/auth, chat, game-state synchronisation for slow-changing data. |
| **UDP** | Low-latency, best-effort — real-time position updates, input broadcasts, fast-changing game state. |

Both protocols can be active simultaneously for a single connection. The engine selects the appropriate protocol for each message category automatically based on registration at startup.

### Authentication System

The `NetworkManager` includes a basic session authentication layer:
- Login / logout with credential validation against the game server.
- Session token management with automatic refresh.
- Disconnect detection and reconnection with session restore.

### Packet Structure

Every packet has a fixed header containing:
- **Sequence number** — detects out-of-order delivery on UDP.
- **Checksum** — CRC-based integrity validation; corrupted packets are silently discarded.
- **Command code** — identifies the packet type for the command dispatch table.
- **Payload length** — allows safe deserialization of variable-length payloads.

### Command Dispatch

The `NetworkManager` uses an extensible command table — game code registers handler functions for each command code, and the manager invokes the correct handler when a packet of that type arrives. This keeps game-specific network logic out of the networking subsystem itself.

### Threading

The network receive and send loops run on `THREAD_NETWORK` managed by `ThreadManager`. All command handler invocations are marshalled to the game-loop thread's safe-invoke queue rather than called directly from the network thread, preventing race conditions on game-state data.

### Statistics

The `NetworkManager` tracks per-session statistics: packets sent/received, bytes sent/received, round-trip time (RTT), packet loss rate, and bandwidth utilisation. These are queryable at runtime and are surfaced in the Debug overlay when `showDebugInfo` is enabled.

[Back to Table of Contents](#table-of-contents)

---

## 25. Gaming AI System

**Source files:** `GamingAI.cpp/.h`  
**Documentation:** [GamingAI-Example-Usage.md](GamingAI-Example-Usage.md)

The `GamingAI` class is a real-time **player behaviour analysis and adaptive AI strategy generation** framework.

### Purpose

Adaptive AI — enemies and game systems that respond intelligently to the specific player's patterns — is a major differentiator in modern games. The `GamingAI` system observes player actions over time, builds a statistical model of player behaviour, and generates adaptive AI commands that counter the player's tendencies.

### Architecture

The system operates on two timescales:

1. **Per-frame real-time data collection** — every frame, the game submits player position, heading, action type, and outcome to the GamingAI's input buffer.
2. **Periodic background analysis** — every `analysisIntervalSeconds` (configurable, default 15 seconds), the `THREAD_AI` worker thread analyses the accumulated data buffer and updates the behaviour model.

### Behaviour Model

The behaviour model tracks:
- **Spatial heat maps** — where the player spends most time on each map.
- **Action frequency histograms** — which actions (fire, dodge, use item) the player performs most often in each scenario.
- **Success/failure rates** — per action type, per map zone, per opponent type.
- **Temporal patterns** — whether the player is more or less aggressive at certain game-clock times.
- **Cross-session learning** — when `enableCrossSessionLearning` is active, the model persists between game sessions and improves over multiple play sessions.

### AI Command Injection

The analysis output is a set of **AI behaviour directives** that can be injected into the enemy AI controller:
- "Player avoids the northern sector — funnel enemy spawns there."
- "Player predominantly uses rapid-fire weapons — increase enemy dodge frequency."
- "Player reaction time is slow (>250 ms) — increase attack rhythm."

These directives are expressed as a structured command set that the game's enemy control logic can query each AI tick.

### Configuration

Key configuration parameters include:
- `maxModelSizeBytes` — caps memory used by the behaviour model (default 256 MB).
- `analysisIntervalSeconds` — frequency of background analysis passes.
- `dataRetentionDays` — how many days of historical session data to retain.
- `learningRate` — how aggressively new observations update the model (0.0–1.0).
- `maxPlayerHistoryEntries` — ring buffer depth for per-player position history.

[Back to Table of Contents](#table-of-contents)

---

## 26. Script Manager

**Source files:** `ScriptManager.cpp/.h`  
**Documentation:** [Scripting-Example-Usage.md](Scripting-Example-Usage.md)

The `ScriptManager` loads, parses, and executes **CPGE Game Script (`.cgs`)** files — plain ASCII/UTF-8 text scripts that drive scene behaviour without requiring C++ code changes.

### Purpose

Not every scene interaction needs to be coded directly in C++. Cutscene sequencing, tutorial flows, mission objective triggers, and level event scripting are better expressed as data-driven scripts that designers or non-programmers can author and modify without touching the engine source. The ScriptManager provides this capability through a simple, purpose-built scripting language.

### Script File Format

Scripts live in `Scripts/<SCENE_NAME>.cgs` and are detected and loaded automatically during scene initialisation. Each file begins with a versioned header:

```
##CPGE_SCRIPT
##ScriptVersion: 1.1
##Scene: SCENE_GAMETITLE
##Author: <author>
##Description: <description>
```

Followed by the script body.

### Command Reference

| Command | Purpose |
| --- | --- |
| `VAR` | Declare a typed variable (`int`, `float`, `bool`, `string`) |
| `FOR / BEGIN / END` | Counted loop block |
| `Execute` | Call a registered C++ engine function by name |
| `QUIT` | Terminate the script |
| `ALERT` | Display a message box or log message |
| `STOP` | Pause script execution |
| `POSITION` | Set a model or camera position by name |
| `PLAY_POSITION` | Jump to a specific XM pattern position |
| `GET_READY` | Trigger the pre-game "get ready" sequence |
| `RESET` | Reset a named variable or state |
| `SAVE / LOAD` | Save or load game state |
| `DETECT_COLLISION` | Register a per-frame collision rule |
| `WAIT` | Suspend execution for N milliseconds |
| `LABEL` | Define a jump target |
| `GOTO` | Unconditional jump to a LABEL |

### Execution Modes

Scripts can execute **synchronously** (blocking the calling thread) or **asynchronously** (in a background thread managed by `ThreadManager`). Per-frame collision rules registered via `DETECT_COLLISION` are evaluated every frame during `ScriptManager::Update(deltaTime)` regardless of synchronous/async mode.

### Engine Function Registration

The `Execute` command calls named C++ functions registered with the ScriptManager at startup. This provides a clean, extensible API surface between the script layer and the C++ engine — adding a new scriptable function requires only a single registration call at init time.

[Back to Table of Contents](#table-of-contents)

---

## 27. File I/O System (FileIO)

**Source files:** `FileIO.cpp/.h`  
**Documentation:** [FileIO-Example-Usage.md](FileIO-Example-Usage.md)

The `FileIO` class provides a **queued, prioritised, asynchronous** file I/O system for all disk access within the engine.

### Purpose

Synchronous file I/O on the render or game-logic thread introduces unpredictable latency spikes from OS disk scheduling. The `FileIO` system queues all disk operations, executes them on the `THREAD_FILEIO` worker thread in priority order, and delivers results via callbacks — keeping every other thread responsive.

### Key Capabilities

- **Priority queue** — each I/O request carries a numeric priority. High-priority loads (e.g., the next level's geometry) are serviced before low-priority background operations (e.g., flushing telemetry logs).
- **Full file operations** — read, write, append, delete, copy, move, rename, and directory create/list/delete.
- **PUNPack integration** — read operations can transparently decompress data packed with `PUNPack` before delivering it to the callback (see Section 33).
- **Statistics** — the `FileIO` system tracks bytes read/written, operation counts, queue depth, and peak queue wait times, all accessible at runtime.
- **Error handling and recovery** — failed operations invoke an error callback rather than throwing; game code decides how to recover without crashing the I/O thread.

### Queue Management

The task queue is a `std::priority_queue` of `FileIOTask` records. Each task specifies: operation type, file path, optional in-memory buffer, priority level, success callback, and error callback. The worker thread pops tasks from the queue, executes them, and fires the appropriate callback.

[Back to Table of Contents](#table-of-contents)

---

## 28. Configuration System (GameConfig.cfg)

**Source files:** `Configuration.cpp/.h`

The `Configuration` class manages reading, validating, and writing `GameConfig.cfg` — the engine's primary user-configurable settings file.

### Purpose

Runtime settings (resolution, volume levels, control sensitivity, renderer type, display mode) should be adjustable without recompiling the engine. `GameConfig.cfg` is a human-readable JSON file that the engine reads at startup and writes back on clean exit.

### File Format

`GameConfig.cfg` is a flat JSON object. Example extract:

```json
{
    "rendererType": 2,
    "resolutionWidth": 800,
    "resolutionHeight": 600,
    "refreshRate": 60,
    "enableVSync": true,
    "masterVolume": 64,
    "musicVolume": 16,
    "fov": 60.0,
    "nearPlane": 0.1,
    "farPlane": 1000.0,
    "showDebugInfo": true,
    "chksum": 1.288e+16
}
```

### Anti-Tampering Checksum

The configuration file is protected by a **floating-point checksum** (`chksum` field). At startup, the engine recomputes the checksum over all other fields and compares it against the stored value. If they do not match, the configuration is treated as tampered or corrupted and is reset to defaults before proceeding. This prevents users from injecting impossible values (e.g., negative volumes, out-of-range resolutions) that would crash the engine.

### Member Access Pattern

All configuration fields are nested inside a `MyConfig myConfig` member of the `Configuration` class. Access always uses `config.myConfig.<field>` — accessing `config.<field>` directly is incorrect and will not compile.

### Key Configuration Fields

| Field | Type | Purpose |
| --- | --- | --- |
| `rendererType` | int | 0=DX11, 1=DX12, 2=OpenGL, 3=Vulkan |
| `resolutionWidth/Height` | int | Window / fullscreen resolution |
| `refreshRate` | int | Target refresh rate (Hz) |
| `enableVSync` | bool | V-Sync on or off |
| `masterVolume` | int | Global audio master volume (0–64) |
| `musicVolume` | int | XM / MP3 music volume |
| `ambientVolume` | int | Ambient SFX volume |
| `dialogVolume` | int | Dialogue / TTS volume |
| `fov` | float | Camera field of view in degrees |
| `nearPlane / farPlane` | float | Projection clipping planes |
| `joystickSensitivity` | float | Gamepad axis sensitivity |
| `showDebugInfo` | bool | Enable/disable debug overlay |
| `displayMode` | int | Windowed / fullscreen / borderless |
| `msaaEnabled / antiAliasingEnabled` | bool | MSAA / FXAA toggles |

[Back to Table of Contents](#table-of-contents)

---

## 29. Exception Handler & Debug System

**Source files:** `ExceptionHandler.cpp/.h`, `Debug.cpp/.h`  
**Documentation:** [ExceptionHandler-Example-Usage.md](ExceptionHandler-Example-Usage.md)

The `ExceptionHandler` and `Debug` classes together provide CPGE's comprehensive crash reporting and diagnostic logging infrastructure.

### ExceptionHandler

The `ExceptionHandler` installs low-level OS exception hooks at startup that intercept **both C++ exceptions and platform-level signals/SEH exceptions**:
- On Windows: `SetUnhandledExceptionFilter` (SEH) + `std::set_terminate` (C++ exceptions).
- On Linux/macOS: `sigaction` handlers for `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL`, `SIGABRT`.
- On Android: NDK signal handlers with `/proc/self/maps` backtrace resolution.

On any unhandled fault, the handler writes a human-readable `Except-CallStack.log` to disk containing:
- Exception type, code, fault address, PID, TID, platform, and timestamp.
- Up to **25 annotated stack frames** resolved from symbol tables (DbgHelp on Windows, `libunwind` / `backtrace()` on Linux/macOS).
- A **breadcrumb trail** of the last N function names tracked via `RECORD_FUNCTION_CALL()` macro — these are the most recently-entered high-level functions, giving immediate context about what the engine was doing when it crashed.

**Debug vs Release:** The exception handler is only active in `_DEBUG` builds. Release builds skip writing the log entirely for performance and to avoid exposing internal symbols in shipped executables.

### Crash Dump Generation

In addition to the text log, the handler can generate a `.dmp` minidump file (Windows MiniDumpWriteDump) for post-mortem analysis in WinDbg or Visual Studio's crash dump viewer.

### Debug Logging System

The `Debug` class provides a structured logging interface used throughout every subsystem:

```cpp
debug.logLevelMessage(LogLevel::LOG_INFO,     L"Scene loaded successfully.");
debug.logLevelMessage(LogLevel::LOG_WARNING,  L"Texture not found — using fallback.");
debug.logLevelMessage(LogLevel::LOG_ERROR,    L"Failed to create vertex buffer.");
debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Renderer initialisation failed.");
```

Messages are written to:
- The **in-game console window** (if active, see Section 30).
- A **log file** on disk.
- The **Visual Studio Output window** via `OutputDebugStringW` in Debug builds.

Shader fallback diagnostics colour-code the failing object differently on screen (e.g., bright magenta) so broken shaders are visually identified without searching log files.

[Back to Table of Contents](#table-of-contents)

---

## 30. Console Window

**Source files:** `ConsoleWindow.cpp/.h`

The `ConsoleWindow` class provides an **in-game overlay console** similar to the developer console found in id Software and Valve engines — a drop-down terminal that renders on top of the game view, accepting typed commands and displaying log output in real time.

### Purpose

During development, being able to execute commands, read log output, and inspect state without Alt-Tabbing to Visual Studio's output window is invaluable. The console window provides this without interrupting the render loop.

### Key Features

- **Overlay rendering** — the console is drawn as a 2D panel via the Direct2D / DirectWrite layer, floating above all 3D content without interfering with the 3D render pipeline.
- **Log mirroring** — all `Debug::logLevelMessage` output is simultaneously written to the console window so no log output is ever lost to the file only.
- **Command input** — the text input field accepts command strings dispatched to registered handlers.
- **Scroll history** — the console maintains a ring buffer of the last N log lines, scrollable with the mouse wheel.
- **Toggle key** — the console is shown/hidden by a configurable key binding (default: tilde/backtick).

[Back to Table of Contents](#table-of-contents)

---

## 31. GUI System (GUIManager)

**Source files:** `GUIManager.cpp/.h`, `GUIWindows.cpp/.h`, `GUIConfigWindow.cpp/.h`

The `GUIManager` provides CPGE's built-in **Graphical User Interface** system — windows, buttons, sliders, tabs, and input fields rendered as 2D overlays using the Direct2D / DirectWrite layer.

### Purpose

Every game needs UI — main menus, options screens, in-game HUD, inventory panels, message dialogs. Rather than embedding a third-party UI framework (which would violate the no-external-wrappers philosophy), CPGE implements a lightweight but complete 2D GUI system natively.

### GUI Architecture

The GUI system uses a **layered, priority-sorted blit queue**. Each GUI element is assigned a layer (z-order) and a priority within that layer. The renderer draws elements back-to-front — background panels first, then content elements, then foreground decorations. This ensures correct transparency and overlap without a scene graph.

### Built-In Elements

| Element | Description |
| --- | --- |
| Window | Resizable, draggable panel with title bar and close button |
| Button | Rectangular or round click target with up/down/hover states |
| Slider | Horizontal or vertical value slider |
| Tab control | Multi-page tabbed panel |
| Text label | Static or dynamic text rendered via DirectWrite |
| Image | 2D texture blit with optional transparency |
| Input field | Keyboard text entry with cursor and selection |
| Check box | Boolean toggle control |

### Configuration Window

`GUIConfigWindow` is a built-in configuration options panel that surfaces the most common `GameConfig.cfg` settings (resolution, volume, quality settings) to the end user through the GUI system without requiring them to edit JSON files manually.

### Intuition System

CPGE's GUI is referred to internally as the **Intuition System** — a reference to the Amiga OS's GUI paradigm, reflecting the author's admiration for the original Amiga GUI architecture. The name is used throughout the codebase in comments and variable names to distinguish the engine's own GUI layer from any third-party UI toolkit.

[Back to Table of Contents](#table-of-contents)

---

## 32. Screen Recorder

**Source files:** `ScreenRecorder.cpp/.h`  
**Documentation:** [ScreenRecorder-Example-Usage.md](ScreenRecorder-Example-Usage.md)

The `ScreenRecorder` class provides **real-time game capture to MP4** — both video (from the D3D11 back buffer) and system audio (via WASAPI loopback) — without any third-party capture library.

### Purpose

Built-in screen recording lets players capture gameplay clips directly from the running game without third-party software. It is toggled with a single key press (default: `Home`) and produces a standard MP4 file in the game's working directory.

### Pipeline

```
Render Thread                           Audio Thread (dedicated)
─────────────────────────────────────   ──────────────────────────────────────
IDXGISwapChain1::GetBuffer(0)           WASAPI loopback → IAudioCaptureClient
  └─ CopyResource → STAGING texture      └─ 10ms PCM packets (silence fill)
       └─ Map (CPU read)                      └─ IMFSinkWriter::WriteSample
            └─ Pack BGRA rows                      [MF: PCM → AAC encoder]
                 └─ IMFSinkWriter::WriteSample
                      [MF: ARGB32 → NV12 → H.264]
```

### Encoding Specifications

| Parameter | Value |
| --- | --- |
| Video codec | H.264 (AVC) via Media Foundation |
| Audio codec | AAC via Media Foundation |
| Container | MP4 (`.mp4`) |
| Video colour space | NV12 (YUV 4:2:0) converted from BGRA at capture |
| Audio sample rate | 44.1 kHz stereo |
| A/V sync | Timestamps derived from a shared high-resolution clock; audio silence-filled on underrun |

### On-Screen Recording Indicator

While recording is active, a small blinking indicator is rendered via the GUI system in a corner of the screen so the player knows recording is in progress.

### Threading Model

The audio capture runs on a dedicated thread separate from both the render thread and the THREAD_AUDIO mixer thread to avoid introducing any latency into game audio playback while recording.

[Back to Table of Contents](#table-of-contents)

---

## 33. Compression System (PUNPack)

**Source files:** `PUNPack.cpp/.h`  
**Documentation:** [PUNPack-Example-Usage.md](PUNPack-Example-Usage.md)

`PUNPack` is CPGE's in-house **lossless data compression and packing** system used for asset packaging, configuration serialisation, and network packet compression.

### Purpose

Shipping game assets uncompressed wastes disk space and increases load times. PUNPack provides fast compression / decompression that is tightly integrated with the `FileIO` system's read pipeline — decompression can be applied transparently as part of a file read operation with no extra code in the calling subsystem.

### Capabilities

- **String and wide-string compression** — efficient encoding for text data such as script files and dialogue strings.
- **Structure serialisation / packing** — arbitrary C++ structs are serialised to a compact binary form with type tags for safe deserialisation.
- **Memory buffer compression** — arbitrary byte buffers can be compressed and decompressed with a single API call.
- **CRC / checksum calculation** — PUNPack provides CRC32 and custom checksum routines used by the Configuration system (`chksum` field) and the Network Manager (packet integrity validation).
- **Performance monitoring** — compression ratio and throughput statistics are tracked and available for tuning.

[Back to Table of Contents](#table-of-contents)

---

## 34. Randomiser Utility (MyRandomizer)

**Source files:** `MyRandomizer.cpp/.h`  
**Documentation:** [MyRandomizer-Example-Usage.md](MyRandomizer-Example-Usage.md)

`MyRandomizer` is a deterministic, seeded random number generation system designed specifically for the needs of real-time game logic.

### Purpose

C++'s standard `rand()` is non-reentrant, poorly seeded, and biased on many platforms. `MyRandomizer` wraps `std::mt19937` (Mersenne Twister) with a set of game-focused APIs that simplify common random-number tasks.

### Key Capabilities

- **Seeded integer and float generation** — generates uniform random integers and floats within specified ranges using the Mersenne Twister engine seeded from `std::random_device` at construction.
- **Unique selection** — returns N unique values from a range (without replacement), useful for card deals, random enemy placement without overlap, and loot tables.
- **Target attempt system** — returns `true` with a given percentage probability, useful for hit/miss calculations, item drop chances, and critical hit rolls.
- **Percentage generation** — generates a random integer in [0, 100] for percentage-based checks.
- **Tracker management** — per-instance state tracking allows multiple independent random streams to coexist without interfering.
- **Cross-session repeatability** — when given a fixed seed, `MyRandomizer` produces the same sequence every run — invaluable for reproducible test scenarios and level-generation debugging.

[Back to Table of Contents](#table-of-contents)

---

## 35. Blender Add-Ons

**Source files / Docs:** `Docs/Blender-AddOns/UIBuilder-Example-Usage.md`, `BlenderImports.cpp/.h`

CPGE ships with companion **Blender Add-On tools** that streamline the export pipeline from Blender to engine-compatible formats.

### Purpose

Blender is the recommended 3D authoring tool for CPGE content. Without custom export settings, Blender's default GLTF exporter may produce files that are not optimal for the engine (incorrect axis conventions, unbaked UV transforms, suboptimal material names). The CPGE Blender add-ons automate and enforce the correct export settings.

### UIBuilder Add-On

The `UIBuilder` Blender add-on (`Docs/Blender-AddOns/UIBuilder-Example-Usage.md`) provides:
- A custom export panel in Blender's 3D viewport sidebar that pre-fills the recommended CPGE GLTF export settings.
- Automatic UV transform baking before export so that `KHR_texture_transform` is fully baked into vertex UVs on export.
- Ship orientation pre-check — warns if the model's root transform is not aligned with the engine's expected facing direction before export.
- Material naming validation — ensures all material names are compatible with the engine's texture-slot lookup system.

### BlenderImports.cpp

On the engine side, `BlenderImports.cpp/.h` contains the specific import adaptations needed for Blender-exported GLTF files, including the axis-space conversion from Blender's right-handed Y-up/Z-forward convention to the engine's left-handed Y-up convention, applied at the GLTF node level during parse.

[Back to Table of Contents](#table-of-contents)

---

## 36. Steam Integration (Future)

**Source files:** `MySteam.cpp/.h` (excluded from current build)

Steam SDK integration is prepared but **deliberately excluded from all current builds**. `MySteam.cpp` and `MySteam.h` are present in the repository but are not referenced in `CMakeLists.txt` or `CrossPlatformGameEngine.vcxproj`.

Steam integration will be enabled when the author initiates active release work. At that point, the following Steam features are planned:
- Achievements and leaderboards via `ISteamUserStats`.
- Cloud saves via `ISteamRemoteStorage`.
- Overlay integration via `ISteamFriends`.
- DLC management via `ISteamApps`.

All Steam headers live in the `steam/` directory and are already present in the repository. No third-party build step is required — the Steam SDK ships as header-only declarations backed by `steam_api.lib`.

[Back to Table of Contents](#table-of-contents)

---

## 37. Licensing

CPGE is released as **free-to-use** for personal study, non-commercial, and commercial purposes under the following terms:

1. **Attribution is mandatory.** Any game, application, or product built on CPGE must clearly state that it was built using the *Cross Platform Gaming Engine (CPGE)*. The preferred form of attribution is displaying the CPGE startup screen. At minimum, a credit in the game's about/credits screen or documentation is required.

2. **No charge may be imposed for the engine itself.** If you sell a product built on CPGE, your price should reflect your content and work — not a premium for access to the engine.

3. **Modifications and derivative works are permitted** provided attribution to the original CPGE engine is retained.

4. **No warranty is provided.** CPGE is offered as-is. The author accepts no liability for damages arising from its use.

The `nlohmann/json` library included in CPGE is independently licensed under the **MIT License**. See [https://github.com/nlohmann/json](https://github.com/nlohmann/json) for its terms.

> "This system comes Free-To-Use for study/personal or commercial purposes. All that you are required to do is state in your game or application that the engine is based on the CPGE (Cross Platform Gaming Engine). That is it, folks — the only mandatory thing I ask for using this engine. Please always give recognition to those who ever helped you!"  
> — Daniel J. Hobson, Melbourne, Australia.

[Back to Table of Contents](#table-of-contents)

---

## 38. Contact & Contributions

| Channel | Link |
| --- | --- |
| **GitHub Repository** | [https://github.com/LordAries1972/CPGE](https://github.com/LordAries1972/CPGE) |
| **Project Website** | [https://ultimanium.com/](https://ultimanium.com/index.php?action=cpge) |
| **JSON Parser (Niels Lohmann)** | [https://github.com/nlohmann/json](https://github.com/nlohmann/json) |

Contributions are welcome via Pull Request on GitHub (subject to management approval via the project website). Bug reports and feature requests may be raised as GitHub Issues or on the CPGE forum board on the project website.

> **Note:** The project website blocks VPN and proxy connections. Please access it from a direct internet connection.

---

*Document authored: 14 June 2026*  
*Engine version at authoring: v0.0.1682*  
*Author: Daniel J. Hobson — Melbourne, Australia*

[Back to Table of Contents](#table-of-contents)
