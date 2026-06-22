# SceneManager and GLTFAnimator Usage Guide

This guide documents the current public APIs in `SceneManager.h` and
`GLTFAnimator.h`. Examples use C++17 and assume the normal CPGE engine globals
and renderer already exist.

## Contents

1. [Core concepts](#core-concepts)
2. [Initialization and cleanup](#initialization-and-cleanup)
3. [Loading scenes](#loading-scenes)
4. [Caching and dynamic scenes](#caching-and-dynamic-scenes)
5. [Cameras](#cameras)
6. [Animations](#animations)
7. [Scene state and switching](#scene-state-and-switching)
8. [API reference](#api-reference)
9. [Performance and troubleshooting](#performance-and-troubleshooting)

## Core concepts

### Two model arrays

The two model arrays have different responsibilities:

| Storage | Purpose |
|---|---|
| Global `models[MAX_MODELS]` | Reusable base-model/cache pool containing parsed geometry and rendering resources. |
| `SceneManager::scene_models[MAX_SCENE_MODELS]` | Instances in the current renderable scene. |

Normal parsing populates the current scene and writes reusable data to
`models[]`. Cache-only parsing retains reusable data in `models[]` but leaves
`scene_models[]` empty. `PutModelToScene()` creates visible instances from
that cache without parsing the source geometry again.

Model IDs passed to `GLTFAnimator` are indexes in `scene_models[]`. They are
not indexes in `models[]` or GLTF node indexes.

### Owned and borrowed state

A `SceneManager` owns its scene instances, its `gltfAnimator`, GLTF/GLB
binary animation data, parsed FBX camera list, and scene transition state.
The renderer passed to `Initialize()` remains externally owned. It must
outlive the manager and every call that uses it.

The public scene and animation APIs do not provide internal synchronization.
Parse, cleanup, cache, placement, update, and rendering operations must be
coordinated by the engine.

## Initialization and cleanup

Initialize after creating the selected renderer backend:

```cpp
SceneManager scene;

if (!scene.Initialize(renderer))
{
    // The renderer could not be cast to the backend selected by this build.
    return false;
}
```

`Initialize()` supports DirectX 11, DirectX 12, OpenGL, and Vulkan according
to the active preprocessor configuration. It resets `sceneFrameCounter` and
returns `false` on a renderer type mismatch.

```cpp
scene.CleanUp();
```

`CleanUp()` destroys loaded `scene_models[]` entries. It generally preserves
the global `models[]` cache. On Vulkan it also invalidates cached GPU handles
and releases cached texture references while the device is alive; later parsing
can rebuild those resources. The destructor calls `CleanUp()` once.

## Loading scenes

### Automatic format detection

```cpp
scene.CleanUp();

if (!scene.ParseSceneAutoDetect(L"Assets/Scenes/Hangar.glb"))
{
    return false;
}
```

`ParseSceneAutoDetect()` routes case-insensitive extensions:

| Extension | Parser |
|---|---|
| `.glb` | `ParseGLBScene()` |
| `.gltf` | `ParseGLTFScene()` |
| `.fbx` | `ParseFBXScene()` |

For missing or unknown extensions it recognizes GLB and binary FBX magic bytes.
It cannot recognize extensionless JSON GLTF or ASCII FBX, so use a normal
extension or call the appropriate parser directly.

The second argument is forwarded to the selected parser:

```cpp
scene.ParseSceneAutoDetect(L"Assets/Characters/Guard.fbx", true);
```

### Direct parsers

```cpp
bool glbOK  = scene.ParseGLBScene(L"Assets/Scenes/Hangar.glb");
bool gltfOK = scene.ParseGLTFScene(L"Assets/Scenes/Hangar.gltf");
bool fbxOK  = scene.ParseFBXScene(L"Assets/Scenes/Hangar.fbx");
```

All accept `bool bCacheOnly = false`. The parsers clear prior lights and stale
FBX camera data. GLTF/GLB parsing also detects the exporter, configures Blender
coordinate conversion, and parses cameras, lights, materials, textures, scene
hierarchy, and animations. FBX parsing uses the engine's FBX 7.x binary/ASCII
importer without the Autodesk SDK.

When matching GPU-ready entries already exist in `models[]`, the parser can
take a geometry fast path. It still performs a lightweight source parse first
to refresh cameras, lights, materials, exporter configuration, and animation
data. `bLoadedFromCache` is `true` only when that fast path was used.

### Result state

```cpp
if (scene.ParseSceneAutoDetect(scenePath))
{
    const bool usedCache = scene.bLoadedFromCache;
    const bool hasAnimations = scene.bAnimationsLoaded;
    const std::wstring& exporter = scene.GetLastDetectedExporter();
    const bool sketchfab = scene.IsSketchfabScene();
}
```

- `bLoadedFromCache` resets at the beginning of every parse.
- `bAnimationsLoaded` reports whether the current GLTF/GLB animations parsed.
- `GetLastDetectedExporter()` returns the latest detected generator or
  `L"Unknown"`.
- `IsSketchfabScene()` tests whether that value is `L"Sketchfab"`.

### Finding a loaded root

```cpp
const int playerID = scene.FindParentModelID(L"PlayerArmature");
if (playerID < 0)
{
    // No loaded root has this exact, case-sensitive name.
}
```

Only models whose `iParentModelID == -1` match. The returned
`scene_models[]` index is the correct animation parent ID.

## Caching and dynamic scenes

There are two related caches:

1. The in-memory global `models[]` pool populated by parsing.
2. The optional binary disk cache written by `SaveCache()`.

### Cache-only parsing

```cpp
if (!scene.ParseSceneAutoDetect(L"Assets/Characters/Guard.glb", true))
{
    return false;
}

// Guard data is retained in models[]; scene_models[] remains empty.
```

On a first load, the parser temporarily creates scene entries and GPU resources,
copies reusable data to `models[]`, and clears the temporary scene entries.
On a cache hit it skips restoring `scene_models[]` entirely.

Do not clear or overwrite `models[]` after cache-only loading. In particular,
`bLoadedFromCache == true` means the parser reused that pool.

### Placing a cached model

```cpp
const int guardID = scene.PutModelToScene(
    L"Guard",
    XMFLOAT3(12.0f, 0.0f, -4.0f),
    true,   // Include cached primitive children.
    true);  // Start an associated animation when available.

if (guardID < 0)
{
    return false;
}
```

`PutModelToScene()`:

1. Finds an active, GPU-ready exact-name cache entry, preferring a root.
2. Optionally gathers cached primitive children.
3. Reserves the lowest free scene slots as an all-or-nothing group.
4. Copies geometry, material, texture, and shared GPU-resource state.
5. Preserves cached rotation/scale while replacing translation.
6. Makes the inserted root independent and reparents included children.
7. Sets up rendering and applies current default lighting.
8. Optionally creates and starts an animation instance on the new root.

It returns the new root scene index, or `-1` when the model is unavailable,
not GPU-ready, or the scene lacks enough free slots. If animation is requested,
the cached animation index is used, falling back to animation zero.

All included entries currently receive the same world matrix. The child option
is intended for the loader's primitive-sibling representation, not arbitrary
recursive scene-graph cloning.

### Multiple independent instances

```cpp
std::vector<int> guardIDs;

for (int i = 0; i < 8; ++i)
{
    const int id = scene.PutModelToScene(
        L"Guard",
        XMFLOAT3(static_cast<float>(i) * 3.0f, 0.0f, 10.0f),
        true,
        true);

    if (id >= 0)
    {
        guardIDs.push_back(id);
        scene.gltfAnimator.SetAnimationSpeed(id, 0.85f + 0.05f * i);
    }
}
```

Each root ID owns separate animation playback state. Capacity use is one scene
slot for the root plus one for every included primitive child.

### Disk cache

```cpp
const std::string cachePath = "Cache/models.dat";

scene.LoadCache(cachePath); // false is a normal cache-miss result.

if (!scene.ParseSceneAutoDetect(L"Assets/Characters/Guard.glb", true))
{
    return false;
}

if (!scene.SaveCache(cachePath))
{
    return false;
}
```

Call `LoadCache()` after `Initialize()`. It restores compatible records into
the global `models[]` pool. The source parse is still needed to refresh
per-load cameras, lights, materials, animations, and renderer resources.

`SaveCache()` serializes loaded `models[]` entries, not the current scene
layout. It returns `true` without writing when no cached models exist, and it
does not overwrite an existing file. Cache invalidation/removal is therefore a
caller responsibility.

The header includes magic, cache version, and `sizeof(Vertex)`. `LoadCache()`
returns `false` for missing, stale, corrupt, ABI-incompatible, or unreadable
data; callers should fall back to source parsing.

## Cameras

### Auto-framing

```cpp
scene.AutoFrameSceneToCamera();
scene.AutoFrameSceneToCamera(XMConvertToRadians(50.0f), 1.35f);
```

The function transforms every CPU-side vertex of every loaded scene model by
its world matrix, computes bounds, and positions the camera along negative Z
looking at the center. It also adjusts near/far range.

This is a load/editor operation, not a per-frame call: cost is linear in total
CPU vertex count. It does nothing if no loaded geometry exists and skips camera
changes while the renderer is resizing. FOV is in radians; padding greater than
`1.0f` adds space around the scene.

### FBX cameras

```cpp
if (!scene.ParseFBXScene(L"Assets/Cinematics/Intro.fbx"))
{
    return false;
}

for (const ParsedFBXCamera& camera : scene.GetFBXCameras())
{
    // name, position, target, up, fovYDeg, nearPlane, farPlane
}
```

FBX cameras are converted to engine left-handed, Y-up coordinates. The returned
const reference remains owned by `SceneManager`; do not retain it across
another parse or manager destruction. Every scene parse clears the prior list.

```cpp
scene.GotoCamera(L"Camera_CloseUp", false); // Instant.
scene.GotoCamera(L"Camera_Wide", true);     // Smooth position transition.
```

Both modes immediately apply FOV and near/far planes. Instant mode applies
position, target, up, and view matrix. Animated mode applies target/up and
projection, then calls `Camera::JumpTo()` with speed 2 and target focus. The
normal camera update must continue for movement to progress.

Names are exact and case-sensitive. `GotoCamera()` returns `false` without an
initialized renderer or when the named camera is absent.

## Animations

GLTF/GLB animation data is exposed through `scene.gltfAnimator`. It supports
translation, quaternion rotation, and scale channels with LINEAR, STEP, and
CUBICSPLINE interpolation.

### Update once per frame

```cpp
void UpdateGame(SceneManager& scene, float deltaTimeSeconds)
{
    scene.UpdateSceneAnimations(deltaTimeSeconds);
}
```

Use seconds, not milliseconds. The call delegates only when
`bAnimationsLoaded` is true. Do not update the same animator twice per frame.

### Inspect and start clips

```cpp
for (int i = 0; i < scene.gltfAnimator.GetAnimationCount(); ++i)
{
    const GLTFAnimation* animation = scene.gltfAnimator.GetAnimation(i);
    if (animation)
    {
        // Read animation->name, duration, samplers, and channels.
    }
}

const int actorID = scene.FindParentModelID(L"Actor");
if (actorID >= 0 && scene.gltfAnimator.GetAnimationCount() > 0)
{
    if (scene.gltfAnimator.StartAnimation(actorID, 0))
    {
        scene.gltfAnimator.SetAnimationLooping(actorID, true);
        scene.gltfAnimator.SetAnimationSpeed(actorID, 1.0f);
        scene.gltfAnimator.SetAnimationDirection(
            actorID,
            AnimationDirection::FORWARD);
    }
}
```

`StartAnimation()` creates or reuses the instance for that parent, selects the
clip, starts playback, and resets to the earliest actual sampler key rather than
assuming time zero. New instances default to looping at speed `1.0f`.
`CreateAnimationInstance()` is available when creation must be separate.

`GetAnimation()` returns `nullptr` for an invalid index.
`GetAnimationDuration()` returns zero for an invalid index.

### Pause, resume, stop, reset, and seek

```cpp
scene.gltfAnimator.PauseAnimation(actorID);  // Retains time.
scene.gltfAnimator.ResumeAnimation(actorID); // Continues.
scene.gltfAnimator.StopAnimation(actorID);   // Stops and sets time to 0.
scene.gltfAnimator.ForceAnimationReset(actorID);

const float duration = scene.gltfAnimator.GetAnimationDuration(0);
scene.gltfAnimator.SetAnimationTime(actorID, duration * 0.5f);

const float current = scene.gltfAnimator.GetAnimationTime(actorID);
const bool playing = scene.gltfAnimator.IsAnimationPlaying(actorID);
AnimationInstance* instance =
    scene.gltfAnimator.GetAnimationInstance(actorID);
```

Pause/resume/stop return `false` if no instance exists.
`ForceAnimationReset()` stops at the actual first key time.
`StopAnimation()` uses literal zero, which can differ for a clip authored with
a non-zero start. `SetAnimationTime()` clamps to `[0, duration]`; the next
animation update evaluates the pose. Missing-instance queries return zero,
`false`, or `nullptr`.

`SetAnimationSpeed()` does not clamp its input. Prefer a non-negative speed
magnitude and use direction controls to express playback direction.

### Direction and endpoint controls

| Direction | Behavior |
|---|---|
| `NONE` | Leaves the current direction unchanged. |
| `FORWARD` | Advances toward the end and respects looping. |
| `REVERSE` | Moves toward the start and respects looping. |
| `BOUNCE` | Alternates between end and start; repeats when looping. |

```cpp
scene.gltfAnimator.SetAnimationDirection(
    actorID,
    AnimationDirection::BOUNCE);

int finalFrame = -1;
if (scene.gltfAnimator.AtAnimationEndFrame(actorID, finalFrame))
{
    scene.gltfAnimator.HoldAnimationAtFrame(actorID, finalFrame);
}
```

Direction changes require an existing instance. `NONE` deliberately does
nothing. Bounce chooses its initial phase from the playback-speed sign.

`AtAnimationEndFrame()` uses the first non-empty sampler. On success it writes
that sampler's final key index; on failure it leaves the output unchanged.
`HoldAnimationAtFrame()` uses the same sampler as its time reference, clamps
the index, seeks, and pauses. The index is not a universal authored frame number
because animation channels can have different key counts.

### Removing animation state

```cpp
scene.gltfAnimator.RemoveAnimationInstance(actorID);

scene.gltfAnimator.ClearAllAnimations();
scene.bAnimationsLoaded = false;
```

`RemoveAnimationInstance()` removes one root's playback state.
`ClearAllAnimations()` removes parsed clips and every instance. Parsers manage
this during normal loading, so manual clearing is for custom lifecycle code.
`DebugPrintAnimationInfo()` logs only when animator debug output is enabled.

## Scene state and switching

### Lightweight scene state

```cpp
scene.SaveSceneState(L"Saves/edited-scene.gltb");
scene.LoadSceneState(L"Saves/edited-scene.gltb");
```

`SaveSceneState()` writes a `GLTB` header/version, exporter name, camera
position/target, and each loaded model's ID, name, position, Euler rotation, and
scale. It does not save geometry, materials, textures, lights, animations, full
world matrices, or source paths.

`LoadSceneState()` resolves each saved name against an already populated
`models[]` pool, copies matches into sequential scene slots, restores transform
fields, sets up rendering, and applies current lighting. Missing names are
skipped. Prepare the model pool first:

```cpp
scene.LoadCache("Cache/models.dat");
scene.ParseSceneAutoDetect(L"Assets/Scenes/EditorAssets.glb", true);
scene.CleanUp();

if (!scene.LoadSceneState(L"Saves/edited-scene.gltb"))
{
    return false;
}
```

Treat this binary as an engine-local snapshot rather than an interchange format.

### Logical scene switching

```cpp
scene.bSceneSwitching = true;
scene.SetGotoScene(SCENE_GAMEPLAY);

// At the engine's transition boundary:
scene.CleanUp();
scene.InitiateScene();
```

`SetGotoScene()` only stores the destination. It does not change
`bSceneSwitching`, load assets, or clean the scene. `GetGotoScene()` returns
that destination. `InitiateScene()` commits it to `stSceneType`, resets
`sceneFrameCounter`, and clears `bSceneSwitching`.

Current scene types are `SCENE_NONE`, `SCENE_INITIALISE`,
`SCENE_GAMETITLE`, `SCENE_INTRO`, `SCENE_INTRO_MOVIE`,
`SCENE_GAMEPLAY`, `SCENE_GAMEOVER`, `SCENE_CREDITS`,
`SCENE_HIGHSCORES`, `SCENE_EDITOR`, `SCENE_LOAD_MP3`, and
`SCENE_EXPERIMENT` in debug builds.

## API reference

### SceneManager

| Function | Purpose/result |
|---|---|
| `Initialize(renderer)` | Binds the selected backend; false on type mismatch. |
| `CleanUp()` | Releases current scene instances. |
| `ParseSceneAutoDetect(path, cacheOnly)` | Detects and loads GLB/GLTF/FBX. |
| `ParseGLBScene(path, cacheOnly)` | Loads GLB or prepares its cache. |
| `ParseGLTFScene(path, cacheOnly)` | Loads JSON GLTF or prepares its cache. |
| `ParseFBXScene(path, cacheOnly)` | Loads FBX or prepares its cache. |
| `SaveCache(path)` / `LoadCache(path)` | Saves/restores global model-cache records. |
| `PutModelToScene(...)` | Inserts cached data; returns root scene ID or -1. |
| `FindParentModelID(name)` | Returns exact-name root scene ID or -1. |
| `UpdateSceneAnimations(dt)` | Updates active animation instances. |
| `AutoFrameSceneToCamera(fov, padding)` | Frames loaded CPU geometry. |
| `GetFBXCameras()` | Returns cameras from the latest FBX parse. |
| `GotoCamera(name, animate)` | Applies or moves toward an FBX camera. |
| `SaveSceneState(path)` / `LoadSceneState(path)` | Saves/restores lightweight layout state. |
| `GetLastDetectedExporter()` | Returns the latest GLTF/GLB generator label. |
| `IsSketchfabScene()` | Tests for the Sketchfab exporter label. |
| `SetGotoScene(type)` / `GetGotoScene()` | Stores/reads the next logical scene. |
| `InitiateScene()` | Commits the next scene and resets transition state. |
| `DiagnoseGLBParsing(path)` | Re-parses a GLB and emits debug diagnostics. |

`DiagnoseGLBParsing()` performs a real scene parse and changes scene state. It
is not a read-only validator.

### GLTFAnimator

| Function group | Purpose |
|---|---|
| `ParseAnimationsFromGLTF()` | Parses supported clips; normally called by SceneManager. |
| `CreateAnimationInstance()`, `StartAnimation()` | Creates/starts root playback state. |
| `StopAnimation()`, `PauseAnimation()`, `ResumeAnimation()`, `ForceAnimationReset()` | Controls playback. |
| `SetAnimationSpeed()`, `SetAnimationLooping()`, `SetAnimationTime()` | Changes runtime state. |
| `SetAnimationDirection()` | Selects forward, reverse, or bounce. |
| `AtAnimationEndFrame()`, `HoldAnimationAtFrame()` | Detects/holds a sampler endpoint. |
| `GetAnimationCount()`, `GetAnimation()`, `GetAnimationDuration()` | Queries parsed clips. |
| `GetAnimationInstance()`, `GetAnimationTime()`, `IsAnimationPlaying()` | Queries instances. |
| `RemoveAnimationInstance()`, `ClearAllAnimations()` | Removes runtime/all animation state. |
| `UpdateAnimations()` | Low-level update; prefer SceneManager's wrapper. |

## Performance and troubleshooting

### Performance guidance

- Prefer cache-only parsing plus `PutModelToScene()` for repeatedly spawned
  assets. It avoids repeated geometry parsing and shares rendering resources.
- Keep parsing, cache I/O, model setup, and auto-framing outside frame-critical
  code. These APIs are synchronous.
- Animation updates traverse active instances and model hierarchies. Pause or
  remove instances that do not need updates.
- Include cached children only when the asset needs its primitive group; every
  child consumes a fixed scene slot and adds setup/update work.
- Coordinate loading with rendering. Do not mutate the arrays while another
  thread reads them.

### Cached model cannot be placed

Verify the exact model name, active/initialized/GPU-ready cache state, renderer
initialization, enough free slots for the whole group, and that no code cleared
`models[]` after cache-only parsing.

### Animation does not run

Verify `bAnimationsLoaded`, a valid clip index, a current root scene ID, and
one `UpdateSceneAnimations()` call per frame. Inserting a cached animated model
without its children can omit nodes targeted by animation channels.

### FBX camera lookup fails

Inspect `GetFBXCameras()` immediately after a successful FBX parse and use the
reported name exactly. Any later scene parse clears the list.

### Complete loading flow

```cpp
bool LoadGameplayScene(
    SceneManager& scene,
    const std::shared_ptr<Renderer>& renderer)
{
    if (!scene.Initialize(renderer))
    {
        return false;
    }

    scene.LoadCache("Cache/models.dat");
    scene.CleanUp();

    if (!scene.ParseSceneAutoDetect(L"Assets/Scenes/Gameplay.glb"))
    {
        return false;
    }

    if (!scene.bGltfCameraParsed)
    {
        scene.AutoFrameSceneToCamera();
    }

    return true;
}

void UpdateGameplay(SceneManager& scene, float deltaTimeSeconds)
{
    ++scene.sceneFrameCounter;
    scene.UpdateSceneAnimations(deltaTimeSeconds);
}
```

This ordering keeps renderer binding, optional cache restore, current-scene
cleanup, source-state refresh, camera fallback, and animation updates explicit.
