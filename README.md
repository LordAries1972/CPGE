# Cross Platform Gaming Engine (CPGE)

Welcome to the Cross Platform Gaming Engine (CPGE) repository which was originally co-founded & designed by Daniel J. Hobson of Melbourne, Australia 2023-2026!

## Introduction

CPGE is a powerful and flexible gaming engine designed to work across multiple platforms. It aims to provide an easy-to-use C++ Class Modules interface for game developers while maintaining high performance and scalability.

The idea of this is to give the developer a great starting frame-work to use with a game or system that they want to develop and the objective of this is to make it work across all platforms (Yes a big project!).  To use this successfully, you will need a very good understanding on C/C++ & low level programming, especially on the targeted O/S you want to develop it for as some things can vary from each OS and Renderer Platform.

If you like to know more about me and my history, please goto my website at https://www.ultimanium.com/index.php?action=aboutus

## Important Links

👉 Read the original full author bio here → [docs/ABOUT.md](Docs/ABOUT.md)

👉 Read the full document about CPGE → [docs/About-CPGE.md](Docs/About-CPGE.md)

👉 [Blender Modelling System v4.3 - v5.2](https://www.blender.org/download/)

👉 [Supporting me on my Website](https://www.ultimanium.com/).  Please do NOT use a VPN Accessing this site or you will be blocked!

👉 Recorded Demos on our website → [CPGE Demos](https://www.ultimanium.com/index.php?action=cpge-demos)

👉 [Donate to the CPGE Project](https://ultimanium.com/index.php?action=makedonation) — Your donation directly supports ongoing development and helps keep this project alive and growing. Every contribution, large or small, is deeply appreciated! **Please do NOT use a VPN when visiting our site or you will be blocked by the system.**

## History Related Links

We keep approxiametely about one months of code changes in the 'History' folder so users can see exactly what code changes have been made directly, but we 
record a build history file as well and can be found here below:-

👉 [Release History](ReleaseInfo.md)

## Features

- Support for multiple platforms (Windows (DirectX 11, DirectX 12, OpenGL, Vulkan), macOS & iOS (OpenGL (For Previous supported OS, METAL for the later MAC technological systems), Linux & Android (Vulkan, OpenGL (Linux only)))
- High-performance rendering
- Flexible and modular architecture
- Extensive documentation and tutorials

## What you could achieve with CPGE so far!
With our First Release, you can now create a full working 2D/3D game using the 2D / 3D Rendering system via DirectX 11, DirectX 12, OpenGL & Vulkan under the Windows Platform.  

- CMAKE & VS2022 compiling are currently supported, other compiler support will soon come (GCC for one!).
- Game Configuration Management, 
- Multiple Render Support Pipelines (DirectX11, DirectX12, OpenGL, Vulkan & Metal (DirectX11, DirectX12, Vulkan & OpenGL confirmed working)),
- Various Debug and Console options (F8 Key) / debug.h - help command available now in debug builds on the console.
- In System Configuration Window (Allows you to modify startup/runtime options),
- Music & Sounds Management System, 
- Faders, 2D Partical Explosion, 3D Starfield, 3D Warp Tunnel, Various Text Scrollers, Parallax Scroller, Fireworks, fade phaser effects, along with other great effects and ever growing!
- Threaded File Loader System & outside FileIO Management system - optional choice on how you want to stage your SCENE transitions or even on the run FILE/IO management, 
- Threading and Thread Safety Management, 
- Mouse input, Joystick/Gamepad & Keyboard input monitoring, 
- Player Management and profiling, 
- Pack/Unpacking routines (Will add more various encryption systems when I come too this (Expected on Phase 3-4 of development), 
- Math Precalculation system,
- Networking, 
- Game AI,
- Script Manager allow users to program the system via scripting,
- Screen and Audio Recording (dumps to recording.mp4)
- Scene Management & cacheing,
- Randomizer (RNG System) & 
- Custom Windows and Controls. 

Basically everything you need to create a standard 2D & 3D Based Looking Gaming system on the Windows 10 SP1 / 11 64bit Operating Systems.  Please note that Win32 is not supported (Well not by me anyways)!

- We are now using scenes which consist of all models (including textures, bumps etc), Camera and lighting within a 3D Project.  For you to use this successfully, consider using a 3D Modeller package that can export GLTF 2.0 or FBX 7+ formats (Legacy Direct.x exports/importing will too come regardless of obseltion, but primarily for DX11 users).  

I, myself, uses Blender v4.3 and above which is also free to use.  You can find it under the 'Important Links' section above.  I will add other supportive formats later when the overall system is functioning as it should.

## Documentation
- Documents and example usages to this project can be found within the Docs folder (constantly been updated).

## Demos
Demo Recordings can now be found on our website on the below link, please do not use a VPN accessing this site or you will be blocked.

👉 Recorded Demos on our website → [CPGE Demos](https://www.ultimanium.com/index.php?action=cpge-demos)

## Known Issues 25th June, 2026:
These are the following known issues and will be resolved as humanly possible.

1) Window Resizing (On Windows, now forbidden - What Resolution set in config.cfg is go!).  To change this, you must goto the SCENE_GAMETITLE and visit the configuration system.  AT ALL COST! DO NOT Alter the GameConfig.cfg file manually as its checksum protected!  ANTI-CHEAT Protection! - PUNPACK will be enforced on this file as well very soon so its fully encyrpted!

2) Full Screen Exclusive modes causing all sorts of mayhem at the moment, from readings and analysis, this is due to the push of win messages and causing things to be out of sync, resulting crashing problems - I am currently investigating this issue!

## Getting Started

To get started with CPGE, follow these steps:

1. **Clone the repository:**
   ```sh
   git clone https://github.com/LordAries1972/CPGE.git
   cd CPGE
   ```
2. **Contribution to repository:**
   ```sh
   Only work on files that relate to the CPGE project itself, do NOT start your own game or system
   within the base CPGE folder.  If you would like to contribute to the project, then please contact
   management at our web site and make a pull request.

   If you are wanting to just start a project of your own, please copy the CPGE folder to a new location
   on your drive and use that instead with your chosen IDE / Compiling system.  If you are NOT using
   Visual Studio 2019/2022 then you may need to setup the project in your chosen IDE (CMAKE building now available).
   ```
---

## Who I am Looking for as Contributors

This project's future depends heavily on passionate, skilled developers who want to make a real difference in the open-source game engine space. CPGE is an ambitious, cross-platform C++ game engine targeting Windows, Linux, Android, macOS, and iOS — and to truly deliver on that promise, we need talented contributors across several key areas. If you see yourself in any of the profiles below, **we want to hear from you.**

> To get involved, please visit us at: **[ultimanium.com/index.php?action=cpge](https://ultimanium.com/index.php?action=cpge)** and please visit under the community menu on the top right of the web system, to contact Management for PULL Requests - Thank you!

---

### 1. Game Developers — General & Those Eager to Learn

**Who this is for:**
Whether you are a seasoned game developer or someone who is just beginning to take their C++ and game development skills seriously, CPGE is an ideal place to contribute and grow. We welcome developers at varying experience levels provided you have a genuine passion for game systems and a willingness to work with low-level C++ code.

**What you will be working on:**

- Gameplay systems: player management, AI, scene transitions, scripting hooks
- Game logic frameworks: state machines, event systems, entity management
- 2D and 3D rendering features: particle systems, scrollers, UI controls, HUD elements
- Audio/music integration, input handling (keyboard, mouse, gamepad/joystick)
- Tools: configuration windows, in-game consoles, debug overlays
- Testing and bug fixing across the existing feature set

**What we expect from you:**

- A solid understanding of C/C++ (at least intermediate level — the codebase is largely low-level)
- Willingness to read and understand existing systems before modifying them
- Respect for the modular architecture and coding standards used throughout the project
- Patience — cross-platform development requires careful, considered changes
- Communication via our website contact or GitHub pull requests before making major changes

**What you will gain:**

- Real-world experience with a production-scale C++ game engine
- Exposure to multiple rendering APIs (DX11, DX12, Vulkan, OpenGL, Metal)
- A visible, credited role in a growing open-source project
- A portfolio piece that demonstrates serious low-level game development skills

---

### 2. Serious macOS / iPhone / iPadOS (Metal Framework) Developers

**Who this is for:**
Apple platform development is one of the most technically demanding areas of this project. We are specifically looking for developers who have hands-on experience with Apple's **Metal graphics framework** and who understand the unique constraints of developing for macOS, iPhone, and iPadOS (formerly iTabOS). The Metal renderer skeleton is already in the source — what we need now are developers who can bring it to life.

**What you will be working on:**

- Completing and stabilising the **Metal renderer** (`Metal/` directory already present in the codebase)
- Ensuring render parity with the DX11/Vulkan/OpenGL pipelines for model rendering, lighting, and scene management
- Adapting the engine's threading and memory model to Apple's GCD (Grand Central Dispatch) conventions
- Input handling differences between macOS, iOS, and iPadOS (touch vs. mouse/keyboard)
- Build system integration: CMake support for Xcode-based builds and Apple toolchains
- Testing on physical Apple hardware — simulators are not sufficient for Metal validation
- Shader porting: translating HLSL/GLSL shaders to Metal Shading Language (MSL)

**What we need from you:**

- Active Mac developer with access to macOS hardware (Intel or Apple Silicon — both are important)
- Experience with Xcode, the Metal API, and Apple's performance tools (Instruments, GPU Frame Capture)
- Understanding of Apple's memory management model (ARC) alongside manual C++ memory management
- Ideally, experience shipping or testing on iPhone and/or iPad physical devices
- Familiarity with the differences in filesystem, rendering pipelines, and sandboxing on iOS/iPadOS vs. macOS
- Knowledge of Apple's App Store submission requirements is a plus (for any future distribution pathway)

**Why this matters:**
Apple hardware is increasingly dominant in the creative and gaming space, especially with the Apple Silicon transition dramatically improving GPU performance. Without a functional Metal renderer, CPGE cannot reach the enormous macOS and iOS user base. This contributor role is **critical** to the project's cross-platform ambitions.

---

### 3. DirectX 12 Developers with Capable Hardware

**Who this is for:**
DirectX 12 is one of the most powerful — and most complex — graphics APIs available on Windows. We are looking for developers who not only understand DX12 at a deep level but who also have access to a **modern DX12-capable GPU** (NVIDIA RTX series, AMD RDNA2/3, or equivalent) to properly develop, test, and profile DX12 rendering features. The DX12 renderer is partially implemented; we need developers who can push it to production-ready quality.

**What you will be working on:**

- Completing the **DX12 render pipeline** — resource management, descriptor heaps, command lists and queues
- Fixing the known **DX12 window resizing crash** (currently listed as a known issue)
- Implementing and validating **root signatures and PSO (Pipeline State Object)** management
- GPU memory management: upload heaps, default heaps, and resource barriers
- Multi-threaded command list recording to take full advantage of DX12's parallelism
- Synchronisation primitives: fences, GPU/CPU synchronisation patterns
- Shader compilation pipeline: DXIL, shader reflection, and runtime shader permutations
- Eventually: DXR (DirectX Raytracing) integration for advanced lighting/reflection systems
- Profiling and performance optimisation using tools such as PIX, RenderDoc, and NVIDIA Nsight

**What we need from you:**

- A modern DX12-capable GPU — not optional. DX12 advanced features cannot be meaningfully tested without real hardware
- Solid understanding of the DX12 programming model (command queues, fences, descriptor heaps, resource states)
- Experience with render graph or frame graph concepts is highly desirable
- Familiarity with HLSL shader development and the DXC shader compiler
- Understanding of GPU memory bandwidth, cache coherency, and GPU profiling
- Previous experience with DX11 is helpful for comparing against the existing working DX11 renderer

**Why this matters:**
DirectX 12 is the primary high-performance rendering target on Windows 10/11 and is required for next-generation visual features. The DX12 renderer also forms the foundation for potential Xbox platform support in the future. This is one of the highest-priority contributor roles for Windows-platform quality.

---

### 4. Linux and Android Programmers

**Who this is for:**
Linux and Android represent a massive and growing gaming audience. We need developers who are fluent in Linux system programming, the Android NDK, and who understand the rendering and build system differences that come with targeting these platforms. Both Vulkan and OpenGL are supported on this platform tier — contributors will work across both.

**What you will be working on:**

**Linux:**

- Maintaining and extending the Linux CMake build (`Linux/CMakeLists.txt` already present)
- Ensuring Vulkan and OpenGL renderer compatibility on major Linux distributions (Ubuntu, Fedora, Arch, Debian)
- Window management via Wayland and/or X11 (handling both display servers is important)
- Input handling differences: evdev, libinput, XInput2
- Audio backend integration: PulseAudio, PipeWire, ALSA
- Linux-specific filesystem conventions, shared library (.so) management, and packaging
- Radeon (ROCm/AMDVLK) renderer support on Linux is already on the feature list — contributors with AMD hardware are especially valuable here
- Multi-distribution testing and CI/CD pipeline contributions

**Android:**

- Android NDK integration: adapting the C++ engine core for the Android build system
- Vulkan on Android: VkSurface via ANativeWindow, swapchain management, touch-to-Vulkan input pipeline
- OpenGL ES renderer adaptation for Android's GLES3+ environment
- Android-specific input: touch events, accelerometer, soft keyboard, gamepad (via Android Input API)
- JNI bridging: connecting the C++ engine core to Android Java/Kotlin activity lifecycle
- APK packaging, permissions model, and integration with Android Studio or Gradle-based builds
- Testing across a range of Android API levels (API 28 minimum recommended)

**What we need from you:**

- Strong command-line Linux skills and comfort working outside of GUI IDEs
- Experience with the Vulkan API (the primary renderer on both Linux and Android)
- For Android: familiarity with the NDK, CMake for Android, and the Android activity lifecycle
- Physical Android device access for testing (emulators are insufficient for Vulkan validation)
- Bonus: experience with Linux game distribution platforms (Steam for Linux, itch.io) or Android Play Store submission

**Why this matters:**
Linux gaming has seen explosive growth with Steam Deck and Proton, and Android represents the single largest mobile gaming platform on the planet. Without solid Linux and Android support, CPGE cannot fulfil its core cross-platform mission. These contributors directly expand the engine's potential audience by hundreds of millions of users.

---

### How to Contribute

1. Visit our project page: [ultimanium.com/index.php?action=cpge](https://ultimanium.com/index.php?action=cpge)
2. Review the existing codebase and identify an area that aligns with your expertise
3. Contact us via the website before beginning significant work, so efforts are coordinated
4. Submit pull requests against the main repository at: [github.com/LordAries1972/CPGE](https://github.com/LordAries1972/CPGE)
5. Clearly describe what you changed, why, and which platform(s) you tested on

All contributors will be credited in the project. Serious, sustained contributors may be invited into a core team role.

---
