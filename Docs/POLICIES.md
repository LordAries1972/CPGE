# CPGE Engine - ENFORCED POLICIES
*Authoritative Policy Definition for Cross Platform Gaming Engine (CPGE)*  
*Generated for: Daniel Hobson (LordAries1972)*  
*Version: 2025.04.20-SCM/ACM*

## 🚫 Absolute Enforcement - Development Policy Rules

1. Code must compile and support Windows 10+ under Visual Studio 2022 using C++17.
2. All debug output must be done via `logLevelMessage()` or `logDebugMessage()`.
3. All rendering must use `scene_models[]` only. `models[]` is for base templates only.
4. Textures must be bound using `Model::GLTF_LoadTexture()` with correct `ModelInfo.materials` linkage.
5. Models must be loaded, processed, and rendered via `ModelInfo` only.
6. Lighting and Cameras must be parsed and handled via `LightManager` and `Camera`.
7. Threading must always use `ThreadManager.cpp` and `ThreadManager.h`.
8. All insertions into base files must include line numbers (`Rule #36`).
9. `.usdc` and USD scene formats are deprecated — `.GLTB` and `.GTB` are mandatory.
10. No external USD libraries may be used. In-house GLTF handling is mandatory.
11. `SetupModelForRendering(info)` must be called only after `ModelInfo` is populated.
12. SceneManager must copy from `models[]` into `scene_models[]` using `SetSceneModelForRendering()`.
13. Never remove valid comments; extend only.
14. All debug calls must be wrapped in `#if defined(_DEBUG_RENDERER_)` blocks.
15. Base model geometry is cached in `models[]` and never rendered.
16. All transforms must apply per-instance using `worldMatrix`, `viewMatrix`, and `projectionMatrix`.
17. Parsing logic must fully resolve bufferViews, accessors, and glTF binary before any render calls.
18. All cameras default looking towards -Z unless transformed.
19. Lights not bound to nodes must be treated as globals.
20. Fallback textures must not be applied unless explicitly required.

---
## 🔒 Authoritative Base File Set
Includes all files that define the active working engine:

- DX11Renderer.cpp / DX11Renderer.h
- ModelPShader.hlsl / ModelVShader.hlsl
- Models.cpp / Models.h / ConstantBuffer.h
- SceneManager.cpp / SceneManager.h
- main.cpp / Configuration.cpp / Configuration.h
- Debug.cpp / Debug.h / Includes.h
- Lights.cpp / Lights.h
- IOStreamThread.cpp / ThreadManager.h
- DXCamera.cpp / DXCamera.h
- SoundManager.cpp / SoundManager.h
- WinMediaPlayer.cpp / WinMediaPlayer.h
- XMMODPlayer.cpp / XMMODPlayer.h

---
All development must fully adhere to these enforced rules and this base set.
