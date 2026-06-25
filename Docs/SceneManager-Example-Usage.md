# SceneManager, ModelAnimator, and FBXAnimator Usage Guide

This guide documents the current public APIs in `SceneManager.h`,
`ModelAnimator.h`, `GLTFAnimator.h`, and `FBXAnimator.h`. Examples use C++17
and assume the normal CPGE engine globals and renderer already exist.

## Contents

1. [Core concepts](#core-concepts)
2. [Choosing the right workflow](#choosing-the-right-workflow)
3. [Use-case scenarios](#use-case-scenarios)
4. [Initialization and cleanup](#initialization-and-cleanup)
5. [Loading scenes](#loading-scenes)
6. [Caching and dynamic scenes](#caching-and-dynamic-scenes)
7. [Cameras](#cameras)
8. [Animations](#animations)
9. [Scene state and switching](#scene-state-and-switching)
10. [API reference](#api-reference)
11. [Performance and troubleshooting](#performance-and-troubleshooting)

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

Model IDs passed to `ModelAnimator` are indexes in `scene_models[]`. They are
not indexes in `models[]` or GLTF/FBX node indexes.

### Owned and borrowed state

A `SceneManager` owns its scene instances, its `modelAnimator` (which contains
a `GLTFAnimator` and an `FBXAnimator`), GLTF/GLB binary animation data, parsed
FBX camera list, and scene transition state.
The renderer passed to `Initialize()` remains externally owned. It must
outlive the manager and every call that uses it.

The public scene and animation APIs do not provide internal synchronization.
Parse, cleanup, cache, placement, update, and rendering operations must be
coordinated by the engine.

## Choosing the right workflow

Choose the workflow from the ownership and visibility required by the asset:

| Requirement | Recommended API set | Do not use it when |
|---|---|---|
| Load a complete authored level | `Initialize()`, `CleanUp()`, `ParseSceneAutoDetect(path, false)` | Objects must remain hidden until individually spawned. |
| Preload reusable static props | `ParseSceneAutoDetect(path, true)`, then `PutModelToScene()` | The complete authored hierarchy should become visible immediately. |
| Spawn multiple cached copies | `PutModelToScene()` once per copy | The model has not first been parsed into `models[]`. |
| Restore geometry between runs | `LoadCache()`, source parse, `SaveCache()` | Restoring the current scene layout; use scene state instead. |
| Restore camera/model transforms | `SaveSceneState()`, `LoadSceneState()` | Geometry, materials, lights, or animations must be serialized. |
| Use an authored FBX camera | `ParseFBXScene()`, `GetFBXCameras()`, `GotoCamera()` | Another scene has been parsed since the FBX. |
| Frame a scene without a camera | `AutoFrameSceneToCamera()` once after loading | Calling every frame or CPU vertex data is unavailable. |
| Play GLTF/GLB animation | `FindParentModelID()`, `StartAnimation()`, `UpdateSceneAnimations()` | The parent is absent from the current scene hierarchy. |
| Change logical application state | `SetGotoScene()`, caller cleanup/load, `InitiateScene()` | Expecting SceneManager to load assets automatically. |
| Diagnose a GLB import | `DiagnoseGLBParsing()` during development | Existing scene state must remain untouched. |

```text
Should the complete source file be visible after loading?
|
+-- Yes -> ParseSceneAutoDetect(path, false)
|          Use for levels, menus, and authored cinematics.
|
+-- No  -> ParseSceneAutoDetect(path, true)
           Then use PutModelToScene() for selected cached models.
```

`bCacheOnly` is a loading policy, not a general performance switch. Passing
`true` deliberately leaves the current scene empty.

## Use-case scenarios

These scenarios show the required setup, update order, failure handling, and
the boundaries where a different API should be used.

### Scenario 1: Load a complete gameplay level

**Use when:** one GLB, GLTF, or FBX file describes the level that should render
as authored.

**Do not use when:** the file is merely a library of runtime-spawned models.

**API set:** `Initialize()` -> optional `LoadCache()` -> `CleanUp()` ->
`ParseSceneAutoDetect(..., false)` -> camera fallback -> per-frame animation.

```cpp
class GameplayScene
{
public:
    bool Initialize(const std::shared_ptr<Renderer>& renderer)
    {
        if (!m_scene.Initialize(renderer))
        {
            return false;
        }

        // A cache miss is normal; source parsing remains the fallback.
        m_scene.LoadCache("Cache/models.dat");
        return true;
    }

    bool Load(const std::wstring& levelPath)
    {
        // Stop all render/update access to the old scene before this call.
        m_scene.CleanUp();

        if (!m_scene.ParseSceneAutoDetect(levelPath, false))
        {
            return false;
        }

        if (!m_scene.bGltfCameraParsed)
        {
            m_scene.AutoFrameSceneToCamera(
                XMConvertToRadians(60.0f),
                1.2f);
        }

        return true;
    }

    void Update(float deltaTimeSeconds)
    {
        ++m_scene.sceneFrameCounter;
        m_scene.UpdateSceneAnimations(deltaTimeSeconds);
    }

private:
    SceneManager m_scene;
};
```

Initialize once for the manager's lifetime. Load subsequent levels through a
controlled cleanup/parse transition instead of rebinding the renderer.

### Scenario 2: Preload and spawn static props

**Use when:** props, pickups, or scenery should be cached without becoming
visible immediately.

**Do not use when:** the complete authored scene should remain intact.

**API set:** one or more cache-only parses -> `PutModelToScene()`.

```cpp
bool PreloadStaticProps(SceneManager& scene)
{
    if (!scene.ParseSceneAutoDetect(
            L"Assets/Props/Crates.glb",
            true))
    {
        return false;
    }

    return scene.ParseSceneAutoDetect(
        L"Assets/Props/Pickups.glb",
        true);
}

int SpawnCrate(SceneManager& scene, const XMFLOAT3& position)
{
    return scene.PutModelToScene(
        L"WoodenCrate",
        position,
        true,  // Include imported primitive siblings.
        false);
}

void BuildPropLayout(SceneManager& scene)
{
    const XMFLOAT3 positions[] =
    {
        XMFLOAT3(0.0f, 0.0f, 4.0f),
        XMFLOAT3(2.5f, 0.0f, 4.0f),
        XMFLOAT3(5.0f, 0.0f, 4.0f)
    };

    for (const XMFLOAT3& position : positions)
    {
        if (SpawnCrate(scene, position) < 0)
        {
            // Cache entry unavailable, not GPU-ready, or insufficient slots.
            break;
        }
    }
}
```

Use `bIncChildren == true` for models split into imported primitive siblings.
Use `false` only for an independently renderable cache entry. Every included
child consumes another fixed scene slot.

### Scenario 3: Spawn an animated GLTF/GLB character

**Use when:** the current source load supplied the character's clips and the
inserted hierarchy includes all animation targets.

**Do not use when:** clips came from an earlier, different source file.

**API set:** cache-only parse -> `PutModelToScene(..., true, true)` ->
animation controls -> one per-frame scene animation update.

```cpp
bool PrepareAndSpawnGuard(
    SceneManager& scene,
    const XMFLOAT3& position,
    int& outGuardID)
{
    // Keep this as the active animated source. A later GLTF/GLB parse replaces
    // the animator's clip collection even though geometry remains cached.
    if (!scene.ParseSceneAutoDetect(
            L"Assets/Characters/Guard.glb",
            true))
    {
        return false;
    }

    outGuardID = scene.PutModelToScene(
        L"Guard",
        position,
        true,  // Include nodes/primitives required by the animation.
        true); // Start the cached animation index.

    if (outGuardID < 0)
    {
        return false;
    }

    scene.gltfAnimator.SetAnimationLooping(outGuardID, true);
    scene.gltfAnimator.SetAnimationSpeed(outGuardID, 1.0f);
    scene.gltfAnimator.SetAnimationDirection(
        outGuardID,
        AnimationDirection::FORWARD);
    return true;
}

void UpdateAllCharacters(
    SceneManager& scene,
    float deltaTimeSeconds)
{
    // This updates every instance; do not call it once per character.
    scene.UpdateSceneAnimations(deltaTimeSeconds);
}
```

Geometry from several files can accumulate in `models[]`, but
`SceneManager::gltfAnimator` holds one current parsed clip collection. A later
GLTF/GLB parse clears/replaces that collection. The current API is therefore not
a multi-file animation bank. Animated assets from different files need
deliberate source ownership or a separate animation-resource layer.

### Scenario 4: Play once and hold the final pose

**Use when:** a door, switch, attack, or interaction should stop on its last
sampled pose.

**Do not use when:** completion must be driven by authored events. The endpoint
API observes sampler time, not animation events.

```cpp
bool StartDoorAnimation(
    SceneManager& scene,
    int& outDoorID,
    int animationIndex)
{
    outDoorID = scene.FindParentModelID(L"Door");
    if (outDoorID < 0)
    {
        return false;
    }

    GLTFAnimator& animator = scene.gltfAnimator;
    if (!animator.StartAnimation(outDoorID, animationIndex))
    {
        return false;
    }

    animator.SetAnimationLooping(outDoorID, false);
    animator.SetAnimationDirection(
        outDoorID,
        AnimationDirection::FORWARD);
    return true;
}

bool HoldDoorWhenComplete(SceneManager& scene, int doorID)
{
    int finalKeyIndex = -1;
    if (!scene.gltfAnimator.AtAnimationEndFrame(
            doorID,
            finalKeyIndex))
    {
        return false;
    }

    scene.gltfAnimator.HoldAnimationAtFrame(
        doorID,
        finalKeyIndex);
    return true;
}

// Frame order:
scene.UpdateSceneAnimations(deltaTimeSeconds);
const bool completed = HoldDoorWhenComplete(scene, doorID);
```

The returned index belongs to the first non-empty sampler. It is not a universal
frame number across every animation channel.

### Scenario 5: Drive a cinematic with FBX cameras

**Use when:** camera transforms and projection settings were authored in FBX.

**Do not use when:** another scene was parsed afterward; every parse clears the
stored FBX camera list.

```cpp
bool StartFBXCinematic(SceneManager& scene)
{
    scene.CleanUp();

    if (!scene.ParseFBXScene(
            L"Assets/Cinematics/Intro.fbx",
            false))
    {
        return false;
    }

    const std::vector<ParsedFBXCamera>& cameras =
        scene.GetFBXCameras();
    if (cameras.empty())
    {
        return false;
    }

    // Apply the first shot immediately.
    return scene.GotoCamera(cameras.front().name, false);
}

bool ChangeCinematicShot(
    SceneManager& scene,
    const std::wstring& cameraName)
{
    return scene.GotoCamera(cameraName, true);
}
```

Animated camera movement uses `Camera::JumpTo()`; normal camera updates must
continue. `GotoCamera()` does not sequence shots or track their duration.

FBX animation stacks are now fully supported. `ParseFBXScene()` parses clips
natively via `FBXAnimator` (no GLTF conversion needed) and auto-starts them on
their root models. FBX animations respond to all `scene.modelAnimator.*` calls
identically to GLTF/GLB animations. The `ModelInfo::importType` field (set to
`ImportType::FBX` or `ImportType::GLTF` at parse time) drives automatic dispatch.

### Scenario 6: Save and restore an editor layout

**Use when:** saving camera position and model transforms while base assets are
managed separately.

**Do not use when:** the save must contain geometry, materials, lights, or
animation state.

```cpp
bool SaveEditorLayout(
    SceneManager& scene,
    const std::wstring& savePath)
{
    return scene.SaveSceneState(savePath);
}

bool LoadEditorLayout(
    SceneManager& scene,
    const std::wstring& assetLibrary,
    const std::wstring& savePath)
{
    // Populate models[] without displaying the asset library.
    if (!scene.ParseSceneAutoDetect(assetLibrary, true))
    {
        return false;
    }

    scene.CleanUp();
    return scene.LoadSceneState(savePath);
}
```

Saved entries resolve by exact model name in `models[]`. Renaming an exported
model can make an older snapshot skip it. Treat the binary as engine-local and
invalidate/version it alongside assets.

### Scenario 7: Perform a controlled scene transition

**Use when:** the application changes logical state and controls when old-scene
render/update work stops.

**Do not use when:** expecting transition methods to load or destroy assets.

```cpp
bool TransitionToGameplay(SceneManager& scene)
{
    scene.bSceneSwitching = true;
    scene.SetGotoScene(SCENE_GAMEPLAY);

    // The wider engine must stop jobs reading scene_models[] before this.
    scene.CleanUp();

    if (!scene.ParseSceneAutoDetect(
            L"Assets/Scenes/Gameplay.glb",
            false))
    {
        scene.bSceneSwitching = false;
        scene.SetGotoScene(SCENE_NONE);
        return false;
    }

    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    return true;
}
```

`SetGotoScene()` only stores a value and `InitiateScene()` only commits it.
The caller owns asset loading, effects, timing, and synchronization.

### Scenario 8: Diagnose a failing GLB

**Use when:** a developer needs detailed import diagnostics with debug logging.

**Do not use when:** current scene state must remain unchanged.
`DiagnoseGLBParsing()` performs a real parse.

```cpp
void DiagnoseAsset(
    SceneManager& scene,
    const std::wstring& glbPath)
{
    scene.CleanUp();
    scene.DiagnoseGLBParsing(glbPath);
}
```

The diagnostic returns `void`. Use its logs and inspect the resulting scene.
Production error handling should use the parser's boolean return value instead.

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

Animation is managed through `scene.modelAnimator`, a universal dispatcher that
automatically routes each call to `GLTFAnimator` (for GLTF/GLB models) or
`FBXAnimator` (for FBX models) based on `ModelInfo::importType`.

GLTF/GLB supports translation, quaternion rotation, and scale channels with
LINEAR, STEP, and CUBICSPLINE interpolation. FBX supports translation, Euler
rotation (all 6 rotation orders), and scale channels with Constant/Step and
Linear/Cubic (approximated as linear) interpolation. Both pipelines convert to
engine left-handed Y-up space automatically.

### ModelAnimator dispatcher architecture

```text
scene.modelAnimator
├── GLTFAnimator gltfAnimator   -- owns GLTF/GLB clips and instances
└── FBXAnimator  fbxAnimator    -- owns FBX clips and instances

scene.modelAnimator.IsAnimationPlaying(id)   // checks both
scene.modelAnimator.StartAnimation(id, clip) // dispatches by importType
scene.modelAnimator.UpdateAnimations(dt)     // advances both
```

`modelAnimator.gltfAnimator` and `modelAnimator.fbxAnimator` are public and
can be accessed directly inside `SceneManager` for format-specific APIs
(e.g. `ParseAnimationsFromGLTF`, `ParseAnimationsFromFBX`, `GetAnimation()`).

### ImportType — automatic dispatch

```cpp
// Set automatically at parse time; read by ModelAnimator::StartAnimation.
enum class ImportType : int { NONE=0, GLTF=1, FBX=2 };

ModelInfo::importType   // ImportType::GLTF or ImportType::FBX after parsing
ModelInfo::fbxNodeIndex // Index into FBXScene::models[] (-1 for GLTF models)
ModelInfo::fbxNodeName  // FBX model name string (empty for GLTF models)
```

No caller code changes are needed when swapping a `.glb` for a `.fbx`: the
same `modelAnimator.*` calls work for both formats.

### Update once per frame

```cpp
void UpdateGame(SceneManager& scene, float deltaTimeSeconds)
{
    scene.UpdateSceneAnimations(deltaTimeSeconds);
}
```

Use seconds, not milliseconds. The call delegates to `modelAnimator.UpdateAnimations()`
only when `bAnimationsLoaded` is true. Both sub-animators are advanced in one call.
Do not call `UpdateAnimations()` a second time in the same frame.

### Inspect and start clips

```cpp
// GLTF/GLB: inspect via the gltfAnimator sub-member (SceneManager internal use)
for (int i = 0; i < scene.modelAnimator.gltfAnimator.GetAnimationCount(); ++i)
{
    const GLTFAnimation* anim = scene.modelAnimator.gltfAnimator.GetAnimation(i);
    if (anim) { /* anim->name, duration, samplers, channels */ }
}

// FBX: inspect via the fbxAnimator sub-member (SceneManager internal use)
for (int i = 0; i < scene.modelAnimator.fbxAnimator.GetAnimationCount(); ++i)
{
    const FBXAnimationClip* clip = scene.modelAnimator.fbxAnimator.GetClip(i);
    if (clip) { /* clip->name, duration, channels */ }
}

// Universal: start, control, and query by model ID -- format is auto-detected
const int actorID = scene.FindParentModelID(L"Actor");
if (actorID >= 0 && scene.modelAnimator.IsAnimationPlaying(actorID) == false)
{
    if (scene.modelAnimator.StartAnimation(actorID, 0))
    {
        scene.modelAnimator.SetAnimationLooping(actorID, true);
        scene.modelAnimator.SetAnimationSpeed(actorID, 1.0f);
        scene.modelAnimator.SetAnimationDirection(actorID, AnimationDirection::FORWARD);
    }
}
```

`StartAnimation()` creates or reuses the instance for that parent, selects the
clip, and starts playback. For GLTF clips it resets to the earliest actual sampler
key; for FBX clips it resets to `startTime`. New instances default to looping at
speed `1.0f`.

### Pause, resume, stop, reset, and seek

```cpp
scene.modelAnimator.PauseAnimation(actorID);  // Retains time.
scene.modelAnimator.ResumeAnimation(actorID); // Continues.
scene.modelAnimator.StopAnimation(actorID);   // Stops and sets time to 0.
scene.modelAnimator.ForceAnimationReset(actorID);

const float current = scene.modelAnimator.GetAnimationTime(actorID);
scene.modelAnimator.SetAnimationTime(actorID, current * 0.5f);
const bool playing = scene.modelAnimator.IsAnimationPlaying(actorID);
```

Pause/resume/stop apply to whichever sub-animator owns the instance (both are
tried; the one with a matching instance wins). Missing-instance queries return
zero or `false`.

`SetAnimationSpeed()` does not clamp its input. Prefer a non-negative speed
magnitude and use direction controls to express playback direction.

### Direction and endpoint controls

| Direction | Behavior |
| --- | --- |
| `NONE` | Leaves the current direction unchanged. |
| `FORWARD` | Advances toward the end and respects looping. |
| `REVERSE` | Moves toward the start and respects looping. |
| `BOUNCE` | Alternates between end and start; repeats when looping. |

```cpp
scene.modelAnimator.SetAnimationDirection(actorID, AnimationDirection::BOUNCE);

int finalFrame = -1;
if (scene.modelAnimator.AtAnimationEndFrame(actorID, finalFrame))
{
    scene.modelAnimator.HoldAnimationAtFrame(actorID, finalFrame);
}
```

Direction changes require an existing instance. `NONE` deliberately does nothing.
Bounce chooses its initial phase from the playback-speed sign.

`AtAnimationEndFrame()` queries the owning sub-animator. On success it writes the
final key index of the first non-empty curve; on failure it leaves the output
unchanged. `HoldAnimationAtFrame()` seeks to that key's time and pauses playback.

### Removing animation state

```cpp
// Not exposed on the unified dispatcher — call the specific sub-animator:
scene.modelAnimator.gltfAnimator.RemoveAnimationInstance(actorID);
scene.modelAnimator.fbxAnimator.RemoveAnimationInstance(actorID);

// Wipe all clips and instances from both sub-animators:
scene.modelAnimator.ClearAllAnimations();
scene.bAnimationsLoaded = false;
```

`ClearAllAnimations()` clears both sub-animators. Parsers manage this during
normal loading, so manual clearing is for custom lifecycle code only.
`DebugPrintAnimationInfo()` (GLTF-specific) logs when `_DEBUG_GLTFANIMATOR_` is
defined.

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
