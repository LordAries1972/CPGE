# Plan: Real-time shadows on all four renderers

> Design document carried over from the TSOO project with the 2026-10-09 engine merge. Section 8 describes TSOO game-specific tuning (`PROJECT_ONLY_CODE`) and is **not** part of the CPGE2026 engine.


## Context

The shaders already have PCF shadow *sampling* (b6 `ShadowBuffer`, t8 `shadowMap`, s2 comparison sampler), but **no renderer ever renders a shadow map**. As a result:
- `lightViewProj` is hard-coded to identity ([Models.cpp:2534](Models.cpp#L2534)).
- `shadowMapSRV` / `shadowTexID` are never set, so `useShadowMap` is always 0.

The importers also lose the shadow flags. `ParseFBXScene` reads FBX `CastShadow`, but writes it into `fxActive` ([SceneManager.cpp:5726](SceneManager.cpp#L5726), [5926](SceneManager.cpp#L5926)). Every renderer resets that field to `false` each frame, and `ReceiveShadow` is dropped. glTF has no shadow flags at all. Vulkan lights with just one directional light from a push constant.

**Goal:** working directional, spot and point shadows on DX11, DX12, OpenGL and Vulkan. Each imported model gets cast/receive flags, and shadow cost can be tuned in the Video settings.

**Decisions made:**
- Scope is BOTH. The pipeline, flags, importers and config are ENGINE code (unwrapped). TSOO tuning goes inside `PROJECT_ONLY_CODE`.
- glTF models always cast and receive shadows.
- Budget is **1 directional + 8 spot + 4 point**. It is adjustable in new Shadow options placed under "Mip Mapping".
- Vulkan gets a **full lighting port** first (global lights with dir/spot/point), then shadows.

**Unverified.** I cannot compile or run anything here (NEVER Compile rule). Every change will be checked by reading only. Shader errors will first show up when FXC/DXC/shaderc/GLSL run at your build or run time.

---

## 1. Shared design (engine, all renderers)

### Shadow budget and resources
| Resource | Contents | Size (from Quality setting) |
|---|---|---|
| t8 `dirShadowMap` (Texture2D, depth) | 1 directional light (orthographic) | Low 1024 / Med 2048 / High 4096 |
| t9 `localShadowMaps` (Texture2DArray, depth) | 32 slices = 8 spot + 4 point × 6 cube faces | Low 512 / Med 1024 / High 1024 |

- Resources are always allocated at full capacity (32 slices), so the spot/point caps can change live.
- Quality changes the resolution, so it needs a video restart, like the other Video toggles.
- Medium costs about 16 MB (directional) + 128 MB (local array) of D32 depth memory.

### Point-light cubes as array slices
Each point light uses 6 consecutive slices with per-face view-projection matrices. The face order is +X −X +Y −Y +Z −Z, with a FOV slightly over 90° so PCF taps don't sample past the face edge. The shader picks the face from the major axis of (worldPos − lightPos). This means spot and point lights share one sampling path and one texture, so no cube-array support is needed (which matters on GL 3.3).

### New b6 / binding 6 layout (same in every shader)
The old 80-byte block is replaced:
```
float4x4 lightViewProj;                // directional (field name kept)
float shadowBias, shadowStrength, useShadowMap, shadowMapSize;
float4x4 localViewProj[32];            // spot slices + point faces
int4  lightShadowInfo[8];              // per GLOBAL light: x=0 none/1 dir/2 spot/3 point, y=first slice
float localBias, useLocalShadows, localMapSize, _pad;
```
- The block is 2272 bytes and is std140-safe.
- The index into `lightShadowInfo` is the light's position in the `GetAllLights()` vector the renderer uploads to b3. The planner takes that same vector, so the indices always match.

### Per-light shadow in shaders
- In the **global-light loop**, each `ProcessLight` result is multiplied by `SampleShadowForLight(i, …)`. The current code applies one factor to all direct light.
- Per-model local lights (b1) stay unshadowed.
- The ShadowsOnly debug mode (7) shows the minimum factor across lights.
- The existing `SampleShadow` becomes `SampleDirShadow` / `SampleLocalShadow`.
- UV convention: DX flips Y (`0.5 - 0.5y`). GL and Vulkan use `0.5 + 0.5y`. GL also remaps the depth reference to `z*0.5+0.5`.

### Per-model receive flag
Stored in the existing material padding float, so no buffer changes size:
- `MaterialGPU::pad1` → `receiveShadows` ([ConstantBuffer.h](ConstantBuffer.h)); HLSL `pad1` → `ReceiveShadows`.
- `GLMaterialUBO::_padM1` → `receiveShadows`.
- Vulkan `VKMatUBO::_pad` → `receiveShadows`.

### Per-light cast flag
- `LightStruct::_pad4` (float) → `int castShadows`. Size and offsets are unchanged.
- Shader structs keep the slot as padding; they never read it.

### CPU planner (in `Lights.h/.cpp`, so no new files or build-system edits)
- `struct ShadowFrameData` (API-neutral, row-major DX row-vector float[16] matrices).
- `struct ShadowCaster { float center[3]; float radius; }`.
- `BuildShadowFrame(const std::vector<LightStruct>& lights, const std::vector<ShadowCaster>& casters, const float camPos[3], ShadowFrameData& out)`:
  - Reads the `config.myConfig` shadow settings.
  - Picks the first active directional caster, then spot and point lights up to the caps.
  - Sizes the directional ortho frustum to fit the caster bounds, clamped to Shadow Distance around the camera, with texel snapping to stop shimmer.
  - Uses near/far from light `range` for spot/point.
  - Returns the view list for the depth pass.
- Small self-contained math (LookAtLH / OrthoLH / PerspectiveFovLH / Mul) so it builds without DirectXMath on OpenGL and Linux Vulkan.
- Upload rule (matches the existing b0 conventions):
  - HLSL: upload the transpose and use `mul(v, M)`.
  - GL/Vulkan: upload raw bytes and use `M * v`.

### Model bounds
- New `ModelInfo` fields: `bool castShadows = true; bool receiveShadows = true;`.
- Plus a lazily computed local bounding sphere (`boundsCenter`, `boundsRadius`, `bBoundsValid`), built from `vertices` on first use.
- Each renderer turns it into a world sphere with its own world matrix (DX: `worldMatrix` with `scale` applied in the VS; Vulkan: its own world build at [VULKAN_RenderFrame.cpp:169](VULKAN_RenderFrame.cpp#L169)).
- Point/spot casters outside the light's range are skipped.

### Depth-pass vertex transform (same on all APIs)
- Constants are `M = World * LightViewProj` (64 B) plus `scale` (16 B) = 80 bytes. That fits Vulkan push constants (128 B guaranteed) and DX12 root constants (20 DWORDs).
- Each renderer has a tiny inline depth-only shader: `clip = mul(float4(pos*scale,1), M)`.
- There is no pixel shader (GL uses an empty fragment shader).
- Slope-scaled raster depth bias is applied, and double-sided (no culling) rendering is used.

---

## 2. Importers, cache and lights (engine)

- [Models.h](Models.h) `ModelInfo`: add the flags and bounds fields above. `CopyFrom` already copies the whole struct.
- [FBXImport.cpp:1240](FBXImport.cpp#L1240) and [1458](FBXImport.cpp#L1458): change the `ReadP70Bool` defaults for `CastShadow`, `ReceiveShadow` and `CastShadows` to **true**. These are the FBX SDK defaults, so a missing property no longer switches shadows off.
- [SceneManager.cpp](SceneManager.cpp), FBX sub-mesh sites 5726 and 5926: set `castShadows` / `receiveShadows` from `fbxModel` instead of `fxActive`.
  - The FBX cache-restore rebind path gets the same treatment.
  - glTF paths (`ParseGLTFScene`) keep the ModelInfo defaults (true/true), per "always on".
- Light creation sites set `castShadows`:
  - `ParseGLTFLights` → 1 (bound and unbound lights).
  - Both FBX light loops (4997, 5407) → `fl.castShadows`.
  - `EnsureDefaultSunLight` → 1.
- Cache ([SceneManager.cpp:6995](SceneManager.cpp#L6995), SaveCache/LoadCache):
  - Add the two flag bytes next to `bFxActive`.
  - Bump `CACHE_VERSION` 1→2. Old `cache.dat` fails the header check once and is rebuilt; the file itself is not touched by me.
- Vulkan material UBO writes ([SceneManager.cpp:4014](SceneManager.cpp#L4014), [4124](SceneManager.cpp#L4124)): fill `receiveShadows`.

---

## 3. DX11 ([DX11Renderer.h/.cpp](DX11Renderer.cpp), [DXRenderFrame.cpp](DXRenderFrame.cpp), [Models.cpp](Models.cpp))

- New renderer members:
  - Dir depth texture (R32_TYPELESS, DSV D32, SRV R32F).
  - Local Texture2DArray with 32 per-slice DSVs and one array SRV.
  - Shadow VS + input layout (compiled inline with `D3DCompile`, as at [DX11Renderer.cpp:3612](DX11Renderer.cpp#L3612)).
  - Depth-pass constant buffer, a biased rasterizer state, a comparison sampler.
  - A renderer-level b6 buffer (2272 B).
- Created in init and released in Cleanup. Not tied to Resize, because shadow maps don't depend on the back buffer.
- `RenderGamePlay` ([DXRenderFrame.cpp:867](DXRenderFrame.cpp#L867)), after the global light upload:
  1. Gather casters.
  2. `BuildShadowFrame`.
  3. Unbind t8/t9.
  4. For each view: set DSV + viewport, draw each caster (`castShadows && m_isLoaded && !proxy`).
  5. Restore the main RTV/DSV, viewport and rasterizer state.
  6. Upload b6, bind the t8/t9 SRVs and s2 once for the frame.
- `Model::Render` ([Models.cpp:2492-2542](Models.cpp#L2492)):
  - Remove the per-model identity b6 write and the t8 bind; the renderer's frame state is used instead.
  - Set `matGPU->receiveShadows`.
  - The per-model `shadowBuffer` / `shadowSamplerState` creation stays for now so nothing else breaks. It becomes unused.
- Shader: `Assets/Shaders/ModelPixel.hlsl` gets the new b6, `Texture2DArray localShadowMaps : register(t9)`, and per-light shadowing. `Includes.h` gets `SLOT_localShadowMap = 9`.

## 4. DX12 ([DX12Renderer.h/.cpp](DX12Renderer.cpp), [DX12RenderFrame.cpp](DX12RenderFrame.cpp), [DX12Models.cpp](DX12Models.cpp))

- Root signature ([DX12Renderer.cpp:1200](DX12Renderer.cpp#L1200)):
  - The model texture table shrinks to **t0–t7** (8).
  - **New root param 8** = descriptor table t8–t9 pointing at renderer-owned shadow SRVs.
  - Model heap allocation stays at 9 slots, so the layout is unchanged.
  - Both `ModelPixel.hlsl` (FXC fallback on FL11) and `DX12NativeModelPixel.hlsl` declare t8/t9, so either PSO is valid.
- New resources:
  - Dir and local depth textures (DEFAULT heap, typeless R32).
  - A small dedicated DSV heap (1 + 32).
  - 2 SRV slots in `m_cbvSrvUavHeap` after the sprite particle slot (I'll check heap capacity when implementing).
  - `m_shadowBuffer` grows to FrameCount × 2272 B, indexed by `m_frameIndex`, so an in-flight frame's data is never overwritten.
- A depth-only PSO + **separate small root signature** (20 root constants for VS) using the inline shadow VS. Root constants version per draw, which avoids the persistently-mapped per-model b0 race.
- `RenderGamePlay` ([DX12RenderFrame.cpp:1164](DX12RenderFrame.cpp#L1164)):
  1. Transition depth maps SRV→DEPTH_WRITE.
  2. Run the depth passes.
  3. Transition back to PIXEL_SHADER_RESOURCE.
  4. Re-set the main root signature, heaps, viewport/scissor, RTV/DSV, and root CBVs b0/b3/b5.
  5. Set b6 + the t8/t9 table.
- `RenderDX12`: set `matGPU.receiveShadows`.
- Shader: `DX12NativeModelPixel.hlsl` gets the same edits as ModelPixel.hlsl.

## 5. OpenGL ([OpenGLRenderer.h/.cpp](OpenGLRenderer.cpp), [OpenGLRenderFrame.cpp](OpenGLRenderFrame.cpp))

- `GLShadowUBO` changes to the new layout (static_assert 2272), and the UBO allocation size is updated.
- Resources:
  - Dir `GL_TEXTURE_2D` depth (`GL_DEPTH_COMPONENT32F`, COMPARE_REF_TO_TEXTURE, LEQUAL, border 1.0).
  - Local `GL_TEXTURE_2D_ARRAY` with the same depth/compare settings.
  - One FBO (`glFramebufferTexture` / `glFramebufferTextureLayer`, `glDrawBuffer(GL_NONE)`).
  - Inline shadow program (330 core) with uniforms `uWorldLightVP` and `uScale`.
- `RenderGamePlay` ([OpenGLRenderFrame.cpp:220](OpenGLRenderFrame.cpp#L220)):
  1. Save the current draw FBO and viewport.
  2. Depth passes with `glPolygonOffset`.
  3. Restore.
  4. Upload the shadow UBO once per frame (replaces the per-model upload at 440-456).
  5. Bind unit 8 = dir map, new unit 9 = local array (`TEXTURE_UNIT_LOCAL_SHADOW`, set the `localShadowMaps` sampler uniform in LoadShaders).
  6. Set `mat.receiveShadows`.
  - Units 8/9 stay bound during the model loop; the per-model unbind loop ([OpenGLRenderFrame.cpp:500](OpenGLRenderFrame.cpp#L500)) is adjusted.
- Shader: `ModelPixel.glsl` in **both** copies (`Assets/Shaders/` and the identical root copy):
  - `sampler2DShadow` for dir, `sampler2DArrayShadow` for local.
  - GL depth remap.
- The embedded fallback `k_3dFragGLSL` gets no shadows (it is only used when the asset shaders are missing). I'll note this.

## 6. Vulkan ([VULKAN_Renderer.h/.cpp](VULKAN_Renderer.cpp), [VULKAN_RenderFrame.cpp](VULKAN_RenderFrame.cpp))

**6a. Lighting port (parity with ModelPixel.glsl):**
- New **set = 2** (renderer-level, one descriptor set per frame in flight):
  - binding 0 GlobalLightBuffer UBO.
  - binding 1 ShadowBuffer UBO.
  - binding 2 dir shadow `sampler2DShadow`.
  - binding 3 local `sampler2DArrayShadow`.
- Persistently mapped host-visible buffers, one pair per frame in flight.
- `k_glsl3DFrag`: replace the `LightPC` push constant with the GlobalLightBuffer loop and the same `ProcessLight` / ambient model as the GL shader.
  - Keeps the ORM and emissive handling Vulkan already has.
  - Push constant range and `VKLightPC` are removed.
  - Pipeline layout becomes {set0, set1, set2}.
- Visual change to expect: Vulkan will now show all scene lights, not just `lights[0]`, and will match DX/GL brightness.

**6b. Shadows:**
- Depth-only render pass (final layout DEPTH_STENCIL_READ_ONLY_OPTIMAL, subpass dependencies to the fragment shader read).
- Dir image and 32-layer array image, with per-layer image views and 33 framebuffers.
- Depth pipeline with depthBias and an 80-byte vertex push constant.
- Comparison sampler.
- Recorded in `RenderFrame` **before** `vkCmdBeginRenderPass` ([VULKAN_RenderFrame.cpp:802](VULKAN_RenderFrame.cpp#L802)), only for 3D scenes with loading finished.
- **Linux:** all new code is platform-neutral (no Win32/DirectXMath). The planner uses its own math, and matrices come from the existing `#if PLATFORM_WINDOWS` world-matrix branches. Linux Vulkan runtime behaviour changes because of 6a; flagging this per the ask-first rule you've already answered.
- `Assets/Shaders/ModelPixel.frag` is dead (never loaded; Vulkan uses the inline GLSL). It is left untouched.

## 7. Settings (engine) — [Configuration.h/.cpp](Configuration.cpp), [GUIConfigWindow.cpp](GUIConfigWindow.cpp)

- `MyConfig` gets these fields (loaded with `j.value(key, default)` so old configs still load):

  | Field | Default | Range |
  |---|---|---|
  | `shadowsEnabled` | true | on/off |
  | `shadowQuality` | 1 | 0 Low, 1 Med, 2 High |
  | `maxSpotShadows` | 8 | 0–8 |
  | `maxPointShadows` | 4 | 0–4 |
  | `shadowDistance` | 200 | 50–1000 |

- They are **not** added to `calculateChecksum`. Adding them would change the hash of every existing GameConfig.cfg and trigger the tamper reset, wiping all settings including the TSOO profile.
- UI: five rows inserted right after "Mip Mapping" in the Video tab ([GUIConfigWindow.cpp:823](GUIConfigWindow.cpp#L823)):
  - "Shadows" toggle.
  - "Shadow Quality" slider (Low/Medium/High); this is the only one that sets `needsVideoRestart`.
  - "Spot Shadows" slider 0–8.
  - "Point Shadows" slider 0–4.
  - "Shadow Distance" slider.
- Tab 2 row count 14 → 19 in `tabContentH` ([GUIConfigWindow.cpp:210](GUIConfigWindow.cpp#L210)); the tab already scrolls.
- All labels are ASCII only.

## 8. TSOO tuning (`#ifdef PROJECT_ONLY_CODE`)

- [IOLoaderThread.cpp:850](IOLoaderThread.cpp#L850) "Sun": set `castShadows = 1` inside a `PROJECT_ONLY_CODE` block.
- **Caveat:** its direction is (0, 1, 0), so light travels *upward* and shadows would be cast downward-up. I'll ask you per-change (as the BOTH rule requires) whether to also fix the direction, before editing.
- IOLoaderThread.cpp already has uncommitted edits of yours; I'll only add lines.

---

## Order of work (each step leaves the build consistent)
1. Shared: Lights.h/.cpp planner + LightStruct flag, ModelInfo fields, ConstantBuffer.h, Includes.h slot, Configuration fields.
2. Importers + cache + light flags (section 2).
3. DX11 + ModelPixel.hlsl.
4. DX12 + DX12NativeModelPixel.hlsl.
5. OpenGL + ModelPixel.glsl (both copies).
6. Vulkan 6a then 6b.
7. Config UI.
8. TSOO tuning (ask first).
9. Save a memory entry for this work (merge implications: CPGE needs the `Assets/Shaders` edits applied separately, because Assets/ is excluded from merges).

All log/debug strings are ASCII only. No build commands will be run.

## Verification (you build and run; I cannot)
1. Build each renderer config.
   - FXC/DXC compile ModelPixel.hlsl / DX12NativeModelPixel.hlsl in the pre-build step, so shader errors appear in the build log.
   - GLSL errors appear in the log at startup. Vulkan shaderc errors are logged as `[VulkanRenderer] Shader compile error`.
2. Load a glTF scene with a sun, a spot and a point light. Expect contact shadows under models on all four renderers.
3. Debug build with `_DEBUG_PIXSHADER_`: key 7 → ShadowsOnly mode (white = lit, grey = shadowed).
4. Video settings: turn Shadows off, set Spot/Point to 0, and change Distance; these apply live. Quality applies after restart.
5. FBX model exported with Cast Shadows off: it should not cast, but should still receive.
6. First run after update: log shows the cache header mismatch, then a full reload (expected, once).
7. DX12 debug layer and Vulkan validation layer: no new errors about resource states or layouts.

## Addendum (2026-09-30): Brightness and Contrast

Requested during implementation.
- **Where:** two new Video-tab sliders, "Brightness" and "Contrast" (each 0.5 - 1.5, default 1.0).
- **Persistence:** `MyConfig::brightness` / `MyConfig::contrast`, saved and loaded by `Configuration` with `j.value()` defaults.
  - They are not in the checksum, for the same reason as the shadow settings.
  - Tab 2 row count 19 -> 21.
- **Rendering:** applying them to the rendered image is a separate question (a post-process pass on each renderer, or a display gamma ramp). It is not covered by this plan until confirmed.
