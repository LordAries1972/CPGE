# Cross Platform Gaming Engine (CPGE)

Welcome to the Cross Platform Gaming Engine (CPGE) repository which was originally co-founded & designed by Daniel J. Hobson of Melbourne, Australia 2023-2026!

Please use the "Phase2" branch as this branch is now locked as the First Phase is now completed where all 4 Render systems working with proper model importing.

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

## History Related Links

We keep approxiametely about one months of code changes in the 'History' folder so users can see exactly what code changes have been made directly, but we 
record a build history file as well and can be found here below:-

👉 [Release History](ReleaseInfo.md)

## Features

- Support for multiple platforms (Windows (DirectX 11, DirectX 12, OpenGL, Vulkan, Radeon), macOS & iOS (OpenGL), Linux & Android (Vulkan, OpenGL & Radeon (Linux only) ))
- High-performance rendering
- Flexible and modular architecture
- Extensive documentation and tutorials

## What you could achieve with CPGE so far!
With our First Release, you can now create a full working 2D game using the 2D / 3D Rendering system via DirectX 11, DirectX12, OpenGL & Vulkan under the Windows Platform.  (Other OS's will be introduced at a later time).

- CMAKE & VS2022 compiling are currently supported, other compiler support will soon come.
- Game Configuration Management, 
- Multiple Render Support Pipelines (DirectX11, DirectX12, OpenGL, Vulkan (DirectX11, DirectX12, Vulkan & OpenGL confirmed working)),
- Various Debug and Console options (F8 Key) / debug.h
- In System Configuration Window (Allows you to modify startup/runtime options),
- Music & Sounds Management System, 
- Faders, 2D Partical Explosion, 3D Starfield, 3D Warp Tunnel, Various Text Scrollers, Parallax Scroller
- File Loader System & FileIO Management system, 
- Threading and Thread Safety Management, 
- Mouse input, Joystick/Gamepad & Keyboard, 
- Player Management, 
- Pack/Unpacking routines, 
- Math Precalculation system,
- Networking, 
- Game AI,
- Script Manager allow users to program the system via scripting,
- Screen and Audio Recording (dumps to recording.mp4)
- Scene Management & cacheing,
- Randomizer (RNG System) & 
- Custom Windows and Controls. 

Basically everything you need to create a standard 2D & 3D Based Looking Gaming system on the Windows 10 SP1 / 11 64bit Operating Systems.

- We are now using scenes which consist of all models (including textures, bumps etc), Camera and lighting within a 3D Project.  For you to use this successfully, consider using a 3D Modeller package that can export GLTF/GLB 2.0 & FBX formats.  I, myself, uses Blender v4.3 and above which is also free to use.  You can find it under the 'Important Links' section above.  I will add other supportive formats later when the overall system is functioning as it should.

## Documentation
- Documents and example usages to this project can be found within the Docs folder (constantly been updated).

## Demos
Demo Recordings can now be found on our website on the below link, please do not use a VPN accessing this site or you will be blocked.

👉 Recorded Demos on our website → [CPGE Demos](https://www.ultimanium.com/index.php?action=cpge-demos)

## Known Issues 17th June, 2026:
These are the following known issues and will be resolved as humanly possible.

1) DX12 Window Resizing is causing a crash.  The other platforms are working fine, so should be a simple fix.

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
