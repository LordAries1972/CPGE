# MyVector / MyQuaternion - Comprehensive Usage Example

## Overview

`Vectors.h` / `Vectors.cpp` provide the engine's full 3D vector toolkit:

| Type / namespace | What it is |
|---|---|
| `MyVector` | 3D vector (position, direction, velocity, normal ...). Exactly 3 contiguous floats, so arrays can be uploaded straight into a vertex or constant buffer on **DirectX 11, DirectX 12, OpenGL and Vulkan**. |
| `MyQuaternion` | Rotation quaternion (axis/angle, Euler, look-at, slerp ...). |
| `MyMath` | Scalar helpers (`Clamp`, `Lerp`, `Repeat`, `PingPong`, `SmoothDamp` ...) plus the table-driven trig the vector code uses. |
| `MyCubeExplosion` | Pooled cube-particle explosion effect: tumbling, drifting, shrinking cubes. Pure math, draw it however your renderer draws cubes. |

The pre-existing `Vector2`, `Vector3` and `Vector4` classes in `Vectors.h` are untouched. `MyVector` converts to and from `Vector3` implicitly.

The code is pure math: it never touches a renderer, so the same calls behave identically on every pipeline.

## Source layout (where everything lives)

The code is split so that the **reusable parts have no dependency on the demo**. Copy the files in the "reusable" rows into another project and they work on their own.

| File | What is in it | Depends on | Reusable? |
|---|---|---|---|
| `Vectors.h` / `Vectors.cpp` | `MyVector`, `MyQuaternion`, `MyMath`, `MyGeom` (`Mat3`, `Rect2`, `HeadingDir`, `FaceAlongX`, `TangentRef`, `Rotate2D`, `WrapInto`, `BounceBlock`, `BounceArena`), `MyCubeExplosion`, `MyExplosionFX`, `MyHighScores` (plus the older `Vector2/3/4`, unchanged at the top). Pure math, no renderer, no platform-specific code. | `Includes.h` (platform / renderer definitions), `MathPrecalculation.h` (lookup tables) | **Yes** |
| `VectorShapes.h` / `VectorShapes.cpp` | The `Mesh` / `Face` types and **every static mesh builder**: primitives (cube, octahedron, ball, rock, arrow, voxel pictures), spacecraft, the fifteen-ship fleet, characters and the seven articulated dungeon creatures, procedural / mapped planets, plus the low-level tools they are built from (`AddFaceAuto`, `AddBar`, `AddTaper` ...). Namespace `MyShapes`. | `Vectors.h`, `Color.h`, `MathPrecalculation.h` | **Yes** |
| `VectorRadar.h` / `VectorRadar.cpp` | **Every radar widget**: `MyStripRadar` (wrapped side-scroller strip), `MyScopeRadar` (round scope), `MyGridRadar` (tile-map mini-map) and the `MyRadarCanvas` drawing interface they draw through. | `Vectors.h`, `Color.h`, `VectorShapes.h` (only for `Shade`) | **Yes** |
| `main.cpp` (inside `#if defined(__USE_VECTOR_DEMO__)`, `namespace VectorDemo`) | The demo itself: the software rasteriser (`SubmitMesh`, `SubmitWorldPoly`, camera, painter's sort), input, sound, the HUD, and the 23 scenes. It contains **no mesh-building and no radar-drawing code any more** - only calls into the three libraries above. | all of the above, `Renderer.h` | No (it is the example) |

`main.cpp` pulls the libraries in with two lines at the top of the demo:

```cpp
#include "VectorShapes.h"
#include "VectorRadar.h"
namespace VectorDemo
{
    using namespace MyShapes;      // Mesh, Face, Shade, BuildCubeMesh(), AddBar(), FleetId ... are used unqualified
```

**How `main.cpp` itself is laid out**, top to bottom: type / capacity constants, `Mat3`, the frame state and mesh globals (`g_cube`, `g_ship`, `g_fleet[]` ... - these hold the meshes the builders return); the rasteriser (`SubmitMesh`, `SubmitMeshGrouped`, `SubmitArrow`, `SubmitWorldPoly`, `SubmitShadow`, `SubmitObject`, `SubmitFloor`); scenes 1-5 (vector demos); *GAME FRAMEWORK* (input, camera helpers, HUD font, `RendererRadarCanvas`); *SOUND EFFECTS*; one section per game in scene order (Asteroids, Guardian, Scoptiliptic, Scramble, Chomper, Planet Assault, Neon Arena, Trench Run, Voxel Guard, Chomper 3D, Marble Madness, Jumpman, Gyrospace, Dungeon, Spiral Tunnel, Morpheous, Dominion, Project X); and finally `Overlay()` (scene switch, start-up mesh building, keys) and the window code. Each game section has its own state struct, reset / update / draw function and HUD function.

## Platforms, include order and merging into another CPGE project

* Every vector file (`Vectors`, `VectorShapes`, `VectorRadar`) includes the global `Includes.h` first, so the same code builds on Windows, Linux, Android, macOS and iOS (whatever `Includes.h` defines as `PLATFORM_*`). Nothing in the vector system itself is platform specific; it also checks for C++17 and sets `NOMINMAX` on Windows.
* **Include order.** On builds without DirectXMath, `Includes.h` includes `Vectors.h` (it aliases `XMFLOAT2/3/4` to `Vector2/3/4`). So `Vectors.h` defines `Vector2/3/4` *first*, includes `Includes.h` right after them, then defines the rest. Either header can be included first. Never define `PLATFORM_*` before including `Includes.h`: it wraps its platform includes in `#ifndef PLATFORM_xxx`.
* `Includes.h` now guards its Windows-only content (`mmreg.h`, `mmsystem.h`, `objbase.h`, `propvarutil.h`) with `PLATFORM_WINDOWS` and supplies an `ARRAYSIZE` fallback, so it can be included on the other platforms.
* **Merging into the base CPGE2026 project.** Its `Vectors.h` is exactly the first part of this one, so ours replaces it as a strict superset. Bring across: `Vectors.h/.cpp`, `VectorShapes.*`, `VectorRadar.*`, the `Includes.h` guards above, and the `MathPrecalculation` additions (`FastSinCosDeg`, `FastACosAccurate`, `GetUnitCircle`). Add the three `.cpp` files to the project / CMake lists (they are already in this project's). There is deliberately no `PROJECT_ONLY_CODE` guard: this is engine-level code.

## Performance and the Vector quality settings

### What was optimised (shared by every game)

All 29 scenes draw through the same software pipeline in `main.cpp`, so that is where the frame time goes. The changes:

| Change | Why it matters |
|---|---|
| Scratch arrays in `SubmitMesh`, `SubmitMeshGrouped`, `SubmitShadow`, `SubmitTerrainShadow` and `SubmitWorldPoly` are `static` | A local `MyVector vv[2700]` runs `MyVector`'s zeroing constructor on every call: about **32 KB zero-filled per call**, even for an 8-vertex cube. A game with a few hundred objects, each with a shadow, wrote tens of MB per frame. `SubmitBooms` calls both functions for every debris cube. |
| Scale folded into the rotation matrix (`Mat3::Scaled`) | One matrix multiply per vertex instead of a scale plus a multiply (meshes, shadows). |
| One perspective divide per vertex instead of one per face corner | A vertex is shared by about three faces. |
| Average polygon depth uses `FAST_INV_INT(cnt)` | A table load and a multiply instead of a divide per face. |
| Explosion directions come from `FAST_UNIT_SPHERE` | No `sin` / `cos` / `sqrt` and no rejection-sampling loop per particle. |
| Explosion easing uses registered power curves (`FastPowCurve`) | No `std::pow` per particle per frame. |
| LOD, shadows, particles and ring detail follow a quality level | The cost can be cut live (below). |

Not measured: nothing here was profiled or run, because this repository is compiled by hand. The changes remove work that was provably wasted (the zero-fill, repeated divides); how much frame time each game gains depends on the scene and the machine. The **Auto** quality level exists so that a game that is still too heavy lowers its own detail until it holds the target rate.

### The settings (Options > Video tab, "Vector ..." rows)

All six apply **immediately** (no restart) and are saved in `GameConfig.cfg`, restored at start-up by `Configuration::loadConfig()`. Older config files without the keys load with the defaults. The keys are not part of the config checksum.

| Row | Config field | Range / default | Effect |
|---|---|---|---|
| Vector Quality | `vectorQuality` | Auto / Low / Medium / High, default Auto | Auto adapts to hold the target FPS; Low / Medium / High fix the detail at 0.25 / 0.6 / 1.0. |
| Target FPS | `vectorTargetFps` | 30 / 60 / 90 / 120, default 60 | Overall frame cap in every quality mode; Auto also adapts detail to this budget. |
| Detail Distance | `vectorDetailDist` | 0.5 - 2.0, default 1.0 | How far away models keep their full detail. 2.0 = twice as far; 0.5 = dots and culling sooner. |
| Vector Shadows | `vectorShadows` | on / off, default on | Drop shadows under models. Shadow hysteresis turns them off below detail 0.25 and on above 0.35. |
| Explosion Particles | `vectorParticles` | 0.25 - 1.0, default 1.0 | Share of debris cubes, sparks and smoke created per blast (the effect size classes are unchanged). |
| Vector Effects | `vectorEffects` | on / off, default on | Shock-wave rings, fireballs and smoke on top of the debris cubes. |

How a detail level (0.1 - 1.0) becomes behaviour (`VecQualityUpdate` in `main.cpp`, once per frame):

| Value | Formula |
|---|---|
| LOD thresholds (dots / culling) | multiplied by `(1 + (1 - detail) * 2.5) / Detail Distance`: 1x at full detail, up to 3.25x at the minimum |
| Shadows | user switch plus hysteresis: off below 0.25, on above 0.35 |
| Particle density | `Explosion Particles * (0.35 + 0.65 * detail)` |
| Shock-wave ring segments | 24 above 0.75, 12 above 0.45, otherwise 8 |

**The Auto governor.** Both rendering paths use `AdaptiveQuality.h`: a time-based
250 ms smoothing window, a downward step of 0.1 after 0.3 s over 104% of the target
budget, and upward probes of 0.05 after sustained on-budget frames (initially 6 s).
One failed probe extends its cooldown once, up to 60 s. Scene/target/mode changes
refit from maximum detail. Raw frames of 250 ms or more are excluded as hitches.
`FramePacer.h` independently applies the selected FPS to every quality mode across
DX11/DX12/OpenGL/Vulkan. Fixed quality does not disable the frame cap. VSync and
actual CPU/GPU throughput can limit achieved FPS; no current runtime capture proves
60 FPS. See [the audio and performance audit](Vector-Audio-Performance-Audit.md)
for the source changes, per-game sound coverage and manual verification procedure.

### Where each setting reaches in the games

| Setting | Applied by | Games affected |
|---|---|---|
| Vector Quality / Target FPS | `VecQualityUpdate` (every frame) derives the detail level and everything below | all 3D scenes |
| Detail Distance + quality: model-to-dot LOD | `SubmitMesh` | every game that draws meshes (all 3D games) |
| Quality: model detail tiers | `PickMeshLod` | Asteroids and Planet Assault rocks (320 / 80 / 20-face models) |
| Quality: planet shading steps (20 or 10) and colour snap (8 or 12 levels per channel, merges near-equal faces into one fill batch) | `SubmitMeshGrouped` | Planet Assault, Gyrospace planets |
| The glow puff (flames, smoke, fireballs, flak, muzzle flares) is drawn by `SubmitMesh` as ONE disc (`SubmitSphereDisc`, 8 / 16 / 32 sides by on-screen size) instead of ~40 visible faces; non-uniformly scaled puffs still use the mesh | `SubmitMesh` | every game that uses `g_puff` |
| Quality: cloud layer tier (subdiv 4 at High, subdiv 3 below) and the atmosphere halo drawn as ONE 32-gon silhouette (`SubmitSphereDisc`) instead of ~2,500 faces | Planet Assault draw code | Planet Assault |
| Vector Shadows | `SubmitShadow` / `SubmitTerrainShadow` / `SubmitHullShadow` / `SubmitTopEllipse`, plus the Jumpman and Dominion ground-shadow polygons | every game with shadows (all of them draw shadows through these) |
| Explosion Particles | `SetDensity` on `g_boom` and `g_fx` | every blast, spark and smoke trail, including the games that spawn them directly |
| Explosion Particles (own particle code) | `MyMath::ScaleCount` in `TpBurst` / `BzBurst` | Tempest and Battle Zone, which keep their own particle pools instead of `g_boom` / `g_fx` |
| Vector Effects | `SubmitFx` and `Boom` | rings, fireballs and smoke in every game, including the games that add rings of their own |

Anything a game draws straight through the renderer (`DrawFilledPolygon`, lines, text, HUD) bypasses the model and shadow paths, so those settings cannot change it; the explosion settings still apply to it if it uses `Boom` or `g_fx`.

### Using the quality controls from your own code

`MyCubeExplosion` and `MyExplosionFX` (in `Vectors.h`) each have `SetDensity(float)`, usable at any time without allocation:

```cpp
static MyCubeExplosion boom(768);
static MyExplosionFX   fx(160);

boom.SetDensity(0.5f);          // Spawn(..., 24, ...) now creates 12 cubes; Spawn(..., 1, ...) (a smoke trail) makes one half of the time
fx.SetDensity(0.5f);            // Sparks() and Smoke() - and so Blast() and Nova() - create half as many
                                // (rings, fireballs and flak puffs are single particles and are unaffected)
                                // Counts are rounded stochastically; a call for two or more particles always makes at least one

float d = boom.Density();       // 0.5
```

To scale your own per-frame work in the same way, read the live values straight from the config (the config window changes them while the game runs):

```cpp
const MyConfig& c = config.myConfig;
const bool  shadows   = c.vectorShadows;
const float particles = static_cast<float>(c.vectorParticles);   // 0.25 .. 1.0
```

## Guardian: arcade sound effects and frenzy

### Frenzy

When the last human dies, every enemy becomes a frenzied hunter. They now chase at **2.6x** their normal speed (it was 1.7x; `kDefFrenzySpeed`) and **shake**: each frenzied ship judders on screen by up to 0.17 world units (`kDefFrenzyShake`) using two fast sines per axis. The shake only moves what is *drawn* (a per-frame copy of the enemy); the AI and the hit tests use the real position, so shooting a shaking ship is as fair as before.

**Mutants** shake as they travel too, all the time (not only in a frenzy), with the same judder.

### Ships dropping into view are harmless

A new wave's ships wait above the screen and glide down (`Lander::enter`, `DefEnter`). While `enter` is true a ship does not hunt, abduct, fire or lay mines, and since this change the **player's ram test ignores it too**: flying into a ship that is still coming in no longer destroys the player's ship.

### Ship speed, radar heights (Guardian and Resogun / Voxel Guard)

* **The player's ship gets faster as the waves go on**, in both games: +4% per wave up to 1.6x at wave 16, applied to top speed, acceleration and climb / dive rate (`shipK = WaveScale(g.wave, 0.04f, 1.6f)`; wave 1 is unchanged).
* **Both radars show real heights.** The strip's vertical axis is the height in the world, from the bottom of the visible field to its top, so each dot sits at the height of the ship, enemy or human it stands for, and a faint line marks the ground base. It is the same code in both games: `MyStripRadar::DotWrapped(..., v)` with `v = (worldY - field.y0) / (field.y1 - field.y0)` and `Baseline(v, colour)`.

### Stix: the Qix's temper

The Qix is faster on every level (24 + 4 per level, capped at 64 cells a second) and has a **temper** from 0 (calm) to 1 (furious):

| | |
|---|---|
| **Rises** | with every successful draw: +0.12 x `temperRate`, **doubled for a slow draw** (the risky one that pays 2.5x). |
| **Rate climbs** | `temperRate` starts at 1.0 + 0.15 per level and grows 10% with every successful draw (up to 3.0), so each draw angers it faster than the last. |
| **Cools** | 0.008 a second, and **halves when the Qix kills you**. A new level starts with temper already (0.07 per level, up to 0.50), so from level 6 it starts aggressive. |
| **Aggressive** | from 0.30 (fully at 0.70) a "THE QIX IS ANGRY!" message appears and the Qix **moves much faster**: speed x (1 + 0.25 x temper + boost x aggression), where the boost is 0.9 on level 1 and rises 0.08 a level to 1.7 (up to 2.25x on level 1, 3x from level 11), capped at 100 cells a second (the update is sliced so it cannot skip the line you are drawing). It **hangs around the marker**: it closes in when far away, circles at about 8-14 cells (the two ends in opposite directions, so the line sweeps round you) and backs off if it gets too close. Its random turns are steadier while it hunts. |
| **Demo bot** | plans cuts rather than dodging: a line counts as safe to draw **fast** or **slow** only if the Qix, at its REAL current top speed (including the temper speed-up), cannot reach it before it closes. Slow-safe lines (2.5x points) are strongly preferred, a fast Qix makes long exposed lines cost more, it samples more candidate lines, gets bold after 5 s without drawing, and only dodges a sparx within 6 steps (and only along a line that is itself safe). |
| **Look** | as temper rises the rainbow Qix bleeds to angry red, its lines thicken (up to +70%), and once aggressive it throbs with a hot glow on each end. |
| **Sound** | the buzz's throb gets faster with temper (up to 20 a second) and deeper, and gets louder the nearer the Qix swings to the marker, so you hear it closing in. |

Its speed is capped at 66 cells a second (about one cell per frame): a faster line could jump clean over the one-cell-wide line being drawn between two frames. All the numbers are in `StxUpdateQix`, `StxClose`, `StxBuzz` and `StxInitLevel`.

### Text size follows the resolution

Every HUD text size comes from `FontPx(H, divisor, minPx)`, which is `max(minPx, H / divisor)`: proportional to the window height, with no upper limit. The sizes used to be `Clamp(H / 42, 11, 20)`, which stopped growing at a window height of 840 px, so at 1080p, 1440p and 4K the text got smaller and smaller relative to the screen.

### Game over returns to demo mode

(Since the hall of fame was added the normal path is automatic: GAME OVER, then - after the name entry for a top-5 score, or 2.5 s later otherwise - the vector high-score table, then demo mode; see "High scores" below. The text that follows describes the fallback when FIRE / START restarts the game early.) When a real game ends, the game no longer restarts as a new scored game: once GAME OVER has been dismissed (its timer runs out, or FIRE / START is pressed, after any high-score name entry) the game returns to **demo (attract) mode** - the autopilot plays, no scores are recorded and the DEMO banner asks for FIRE to start. The fire press that dismissed GAME OVER has to be released before a new press starts a game. This is done in one place for every game: `DrawGameHud` notices a game that was showing GAME OVER and is running again, and calls `StartDemoAfterGameOver()`.

### Arcade sound effects (extracted from the original sound ROM)

Guardian's sound board has no stored samples: it is a Motorola 6800 CPU that *synthesises* every sound in software and writes the result to an 8-bit DAC. So the sounds were produced by running the real ROM (`video_sound_rom_1.ic12` from the MAME `defender.zip`) on a software 6800 and recording the DAC.

| Step | Detail |
|---|---|
| Board model | 6808 at 894,886 Hz; RAM at `$0000-$007F`; an MC6821 PIA at `$0400`; ROM at `$F800`. PIA port A drives the DAC; port B carries the command. |
| Sound commands | The ROM's IRQ handler reads port B and computes `command = (~PB) & $1F`, so commands run 1-31 (0 = idle). A rising edge on CB1 raises the IRQ. Commands 1-13 go through a table, 14-28 through a jump table at `$FD58`, 29-31 through a tone generator. |
| Which commands the game uses | The main board's 6809 code sends sounds through a descriptor table at `$D4AB-$D534` (each entry is a priority byte, then groups of *repeat, duration, command*, then 0). 20 commands appear in it: 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 14, 17, 20, 21, 23, 24, 25, 26, 30, 31. The emulated ROM is silent for exactly the other commands that are unused (19, 27, 28), which is a good sign the emulation is right. |
| Output | `Assets/Defender/df-cmdNN.wav` (NN = the ROM's command), 22,050 Hz, 16-bit mono, level-matched and de-clicked. Command 14 is cut into a seamless loop. |
| REA | REA could not open the ROM (it only reads executable formats, and has no 6800 support), so a throw-away 6800 emulator was used for the extraction. It is not part of the project: only the resulting WAV files are. |

**Which sound plays for which event.** The table in `SfxInit` (`main.cpp`) names the file for each event, so re-pointing an event at a different arcade sound is a one-word change there. The mapping is a **best guess**: the sound numbers are certain, but the game code that links each one to an event was not disassembled, so events were matched to sounds by their priority, length and character (and nobody could listen to them while it was built). Please play the game and retune by ear:

| Event | File | Why this one |
|---|---|---|
| Laser | `df-cmd03.wav` | the shortest sound (0.17 s), queued at the lowest priority like a rapid-fire weapon |
| Thrust (loops while the stick is pushed) | `df-cmd14.wav` | the arcade queues it as ten 1-tick repeats, i.e. a sustained sound |
| Enemy explosion | `df-cmd07.wav` | a one-second noise rumble |
| Player explosion | `df-cmd25.wav` | highest-priority entry, 2.3 s |
| Smart bomb | `df-cmd30.wav` | the other highest-priority entry |
| Human falls | `df-cmd26.wav` | a falling sweep |
| Human caught by the ship | `df-cmd11.wav` | a rising sweep |
| Human landed safely (+500) | `df-cmd08.wav` | three quick rising beeps |
| Lander grabs a human | `df-cmd17.wav` | a falling warble |
| Enemy shot | `df-cmd12.wav` | a steady tone |

The other extracted files (`df-cmd01`, `05`, `06`, `09`, `10`, `20`, `21`, `23`, `24`, `31`) are not used yet: they are the arcade's remaining sounds (enemy appearing, hyperspace, swarmers, baiters, extra life ...) waiting to be attached to events.

**Copyright.** The audio is derived from Williams' copyrighted ROM code. It is generated locally from a ROM you own; keep `Assets/Defender/` out of any public repository or release (for example with a `.gitignore` entry) unless you have the rights to distribute it.

## Where the routines came from (REA provenance)

The routine set was recovered from the shipped Unity build of **Outnumbered** with REA.

| Observation | Source |
|---|---|
| Full `Vector3` surface (69 methods: `Lerp`, `Slerp`, `SmoothDamp`, `Reflect`, `Project`, `ProjectOnPlane`, `Angle`, `SignedAngle`, `RotateTowards`, `OrthoNormalize`, `ClampMagnitude`, `MoveTowards`, operators ...), plus `Vector2`, `Vector4`, `Mathf`, `Quaternion`, `Plane`, `Ray` | `UnityEngine.CoreModule.dll` metadata (enumerated read-only with `System.Reflection.Metadata`; REA's own metadata budget rejected the large assemblies) |
| Game helpers `MathUtil` (`GetReflectionDirectionFromAngle`, `GetLineDirectionFromAngle`, `DirectionTo2dDeg`, `GetNormalFromVel`, `LerpAngle`, `Repeat`, `ClampMagnitude`) and `FORGE3D.F3DMath` (`ProjectVectorOnPlane`, `SignedVectorAngle`) | `Assembly-CSharp.dll`, decompiled with ILSpy |

Design notes (our decisions, not observations):

* Unity's native-only routines (`Slerp`, `RotateTowards`, quaternion maths) are re-implemented from their documented behaviour.
* The game's `MathUtil.ToDeg` multiplies by `PI/180` (a bug in the original). `MyMath::ToDeg` uses the correct `180/PI`.
* Angles at the public API are **degrees**, as in Unity.
* Coordinates follow Unity's convention: `Forward = +Z`, `Up = +Y`, `Right = +X`.

## Precalculated tables used

Everything trigonometric goes through `MathPrecalculation` (`FAST_MATH`). Three tables were added for the vector code:

| New API in `MathPrecalculation` | Purpose |
|---|---|
| `FastSinCosDeg(deg, sin, cos)` / `FAST_SINCOS_DEG` | Degree-domain sin+cos in one lookup. Interpolated, O(1) wrap, no radian conversion. Used by every `Rotated*` call and `AngleAxis`. |
| `FastACosAccurate(v)` / `FAST_ACOS_ACCURATE` | Accurate arccosine for dot-product angles (interpolated half-angle table). The older `FastACos` is ~0.6 degrees off for nearly parallel vectors. Used by `Angle`, `Slerp`, `RotateTowards`. |
| `GetUnitCircle(n)` / `FAST_UNIT_CIRCLE` | Precalculated `(cos, sin)` rings for 3,4,5,6,8,10,12,16,20,24,32,48,64 segments. Builds prisms, cones and discs with zero trig. |

`Initialize()` builds them together with the existing tables. **Call `FAST_MATH.Initialize()` before using `MyVector`** (`main.cpp` already does).

Square roots use the hardware `sqrtss` instruction instead of `FastSqrt`: it is exact and faster than a table lookup plus the table's atomic statistics counter.

## Building and running

All build scripts and VS Code files use paths relative to this folder:

```bat
cmake-build dx11 debug
cmake-build dx12 debug
cmake-build opengl debug
cmake-build vulkan debug
cmake-build all debug
```

* In VS Code run **Terminal > Run Build Task** (tasks in `.vscode/tasks.json`) and launch from the Run panel (`.vscode/launch.json`).
* `build.stale-from-CPGE2026/` is the old CMake output copied from the original project. Its cache is bound to `F:/Projects/C++/CPGE2026`, so it was renamed out of the way. Delete it once you are happy.
* `__USE_VECTOR_DEMO__` at the top of `main.cpp` boots straight into the demo. Comment the line out for the original game flow.

## Quick start

```cpp
#include "Vectors.h"
#include "MathPrecalculation.h"

FAST_MATH.Initialize();                                  // once, at startup

MyVector a(1.0f, 2.0f, 3.0f);
MyVector b = MyVector::Right() * 4.0f;

MyVector sum    = a + b;
float    len    = a.Magnitude();
MyVector unit   = a.Normalized();
float    dot    = MyVector::Dot(a, b);
MyVector cross  = MyVector::Cross(a, b);                 // perpendicular to both
float    dist   = MyVector::Distance(a, b);
float    angle  = MyVector::Angle(a, b);                 // degrees, [0, 180]
```

## API tour with examples

### Constants and construction

```cpp
MyVector::Zero(); One(); Up(); Down(); Left(); Right(); Forward(); Back();
MyVector::PositiveInfinity(); NegativeInfinity();

MyVector v2(1, 2);                                      // z = 0
MyVector fill(5.0f);                                    // (5,5,5) - explicit
MyVector fromEngine = Vector3(1, 2, 3);                 // implicit from engine Vector3
Vector3  toEngine   = fromEngine;                       // implicit back
auto fromDx = MyVector::FromXYZ(someXMFLOAT3);          // any {x,y,z} type
auto toDx   = a.ToXYZ<DirectX::XMFLOAT3>();
Vector4  v4 = a.ToVector4(1.0f);                        // homogeneous point
```

### Arithmetic, indexing, comparison

```cpp
MyVector r = (a + b) * 0.5f - a / 2.0f;                 // division is one reciprocal + 3 multiplies
a += b;  a *= 2.0f;
float x = a[0];                                         // [0]=x [1]=y [2]=z, out of range -> z (never UB)
bool same  = (a == b);                                  // approximate, like Unity (distance < ~1e-5)
bool exact = a.Equals(b);                               // bit-exact component compare
```

### Length and normalisation

```cpp
float len = a.Magnitude();                              // sqrt
float sq  = a.SqrMagnitude();                           // prefer for comparisons: no sqrt
a.Normalize();                                          // in place; vectors shorter than 1e-5 become zero (no NaN)
MyVector n = a.Normalized();
MyVector clamped = MyVector::ClampMagnitude(velocity, maxSpeed);
```

### Projection and reflection

```cpp
MyVector bounce  = MyVector::Reflect(incoming, wallNormal);          // wallNormal must be unit length
MyVector along   = MyVector::Project(v, onto);                       // component of v along `onto`
MyVector slide   = MyVector::ProjectOnPlane(v, planeNormal);         // v with the normal component removed
MyVector slide2  = MyVector::ProjectVectorOnPlane(unitNormal, v);    // game's F3DMath helper (argument order as in the game)
MyVector rest    = MyVector::Exclude(axis, v);                       // v minus its projection onto axis
```

### Interpolation and movement

```cpp
MyVector p = MyVector::Lerp(a, b, 0.25f);               // t clamped to [0,1]
MyVector q = MyVector::LerpUnclamped(a, b, 1.5f);       // extrapolates
MyVector s = MyVector::Slerp(a, b, t);                  // direction sweeps along the arc, length blends
MyVector m = MyVector::MoveTowards(pos, target, speed * dt);
MyVector r = MyVector::RotateTowards(heading, target, maxRadians, maxMagDelta);

// Critically damped follow: keep `vel` between frames.
static MyVector vel;
pos = MyVector::SmoothDamp(pos, target, vel, 0.25f, MyMath::kInfinity, dt);
```

`Lerp` between two perpendicular unit vectors gives a vector shorter than 1 in the middle; `Slerp` keeps the length.

### Angles

```cpp
float ang    = MyVector::Angle(a, b);                           // degrees, always positive
float signed_ = MyVector::SignedAngle(a, b, MyVector::Up());    // sign from the axis (turn left/right)
float rad    = MyVector::AngleBetween(a, b);                    // radians
float f3d    = MyVector::SignedVectorAngle(ref, other, normal); // game's F3DMath version

// Which side is the target on?
float side = MyVector::SignedAngle(MyVector::Forward(), toTarget, MyVector::Up());   // + = right, - = left
```

### Rotating a vector

```cpp
MyVector r1 = v.RotatedY(90.0f);                        // about world Y (degrees)
MyVector r2 = v.RotatedAround(MyVector(1, 1, 0), 45.0f);// Rodrigues, any axis
MyVector r3 = v.Rotated(quat);                          // by a quaternion

// Hot path: rotate MANY vectors with ONE table lookup.
MyVector verts[1024];
MyVector::RotateAroundBatch(verts, 1024, MyVector::Up(), angleDegrees);
```

`RotateAroundBatch` costs a single `FastSinCosDeg`; the loop is pure multiply-add. Use it for meshes, particle clouds and formations.

### Orthonormal bases

```cpp
MyVector n = surfaceNormal, t = someTangentGuess, bt(0, 0, 1);
MyVector::OrthoNormalize(n, t, bt);                     // n, t, bt are now mutually perpendicular unit vectors
```

### Game helpers recovered from `MathUtil`

```cpp
float heading = MyVector::DirectionTo2dDeg(vx, vy);          // [0,360), 0 = +Y, counter-clockwise
MyVector wall = MyVector::ReflectionDirectionFromAngle(heading);   // snaps to Up/Down/Left/Right
MyVector line = MyVector::LineDirectionFromAngle(heading);         // wider up/down cones
MyVector n    = MyVector::NormalFromVelocity(velocity);            // heading -> wall normal in one call
float shortest = MyMath::DeltaAngle(10.0f, 350.0f);                // -20
float blended  = MyMath::LerpAngle(350.0f, 10.0f, 0.5f);           // 0, goes the short way round
```

### Text, persistence, hashing

```cpp
std::string s = a.ToString();                           // "MyVector(1, 2, 3)"
std::string w = a.Serialize();                          // "1,2,3"
MyVector back = MyVector::Deserialize(w);               // zero vector when malformed

std::unordered_map<MyVector, int> grid;                 // std::hash<MyVector> is provided
```

## MyQuaternion

```cpp
MyQuaternion q1 = MyQuaternion::AngleAxis(90.0f, MyVector::Up());
MyQuaternion q2 = MyQuaternion::Euler(30.0f, 45.0f, 0.0f);        // Unity order: Z, then X, then Y
MyQuaternion q3 = MyQuaternion::LookRotation(targetDir, MyVector::Up());
MyQuaternion q4 = MyQuaternion::FromToRotation(MyVector::Right(), dir);   // trig-free; aligns +X onto dir

MyVector rotated = q2 * MyVector::Forward();            // rotate a vector
MyQuaternion both = q1 * q2;                            // apply q2 first, then q1

MyQuaternion mid  = MyQuaternion::Slerp(q1, q2, 0.5f);
MyQuaternion step = MyQuaternion::RotateTowards(current, target, 90.0f * dt);   // 90 deg/s
float between     = MyQuaternion::Angle(q1, q2);        // degrees

MyVector euler = q2.EulerAngles();                      // degrees, [0,360)
float deg; MyVector axis; q2.ToAngleAxis(deg, axis);
MyQuaternion inv = MyQuaternion::Inverse(q2);
```

## Cookbook

### 1. Homing missile with turn-rate limit

```cpp
void UpdateMissile(MyVector& pos, MyVector& heading, const MyVector& target, float speed, float turnDegPerSec, float dt)
{
    MyVector want = (target - pos).Normalized();
    // RotateTowards works in radians: convert the degree turn rate once.
    heading = MyVector::RotateTowards(heading, want, turnDegPerSec * MyMath::kDeg2Rad * dt, 0.0f);
    pos += heading * (speed * dt);
}
```

### 2. Bounce a ball inside a box

```cpp
void BounceInBox(MyVector& p, MyVector& v, const MyVector& boxMin, const MyVector& boxMax)
{
    static const MyVector faces[6] = { MyVector::Right(), MyVector::Left(), MyVector::Up(),
                                       MyVector::Down(), MyVector::Forward(), MyVector::Back() };
    const float limits[6] = { boxMin.x, -boxMax.x, boxMin.y, -boxMax.y, boxMin.z, -boxMax.z };
    for (int i = 0; i < 6; ++i)
    {
        float d = faces[i].Dot(p) - limits[i];
        if (d < 0.0f) { p -= faces[i] * d; if (v.Dot(faces[i]) < 0.0f) v = MyVector::Reflect(v, faces[i]); }
    }
}
```

This is the same technique the demo uses to keep its flying vectors on screen (with the six planes of the view frustum).

### 3. Orbit a camera around a target

```cpp
MyVector OrbitPosition(const MyVector& target, float distance, float yawDeg, float pitchDeg)
{
    return target + MyQuaternion::Euler(pitchDeg, yawDeg, 0.0f) * MyVector(0, 0, -distance);
}
```

### 4. Build a cone or cylinder with the precalculated unit circle

```cpp
const auto& ring = FAST_UNIT_CIRCLE(16);                // 16 (cos, sin) pairs, no trig here
std::vector<MyVector> base;
for (const auto& p : ring) base.push_back(MyVector(p.x * radius, 0.0f, p.y * radius));
```

### 5. Upload vectors to any renderer

```cpp
std::vector<MyVector> points = BuildPoints();
// Three tightly packed floats per element on every API:
//   DX11 / DX12 : D3D11_SUBRESOURCE_DATA / upload buffer, DXGI_FORMAT_R32G32B32_FLOAT
//   OpenGL      : glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(MyVector), points.data(), ...)
//   Vulkan      : memcpy into a VkBuffer, VK_FORMAT_R32G32B32_SFLOAT
static_assert(sizeof(MyVector) == 12, "MyVector must stay tightly packed");
```

## MyCubeExplosion - cube particle explosions

A burst of small cubes thrown out in uniformly random directions. Each cube tumbles about its own axis (several full turns during its life), slows under drag, optionally falls under gravity, and shrinks as it dies. The pool is allocated once in the constructor; `Spawn` and `Update` never allocate. When the pool is full the oldest slots are recycled.

```cpp
static MyCubeExplosion boom(512);                       // once: reserves 512 particles

// On a kill: 24 cubes, up to 6 units/s, 0.22 edge, ~1.1 s, orange. Inherit the victim's momentum.
boom.Spawn(target.pos, 24, 6.0f, 0.22f, 1.1f, 255, 160, 40, target.vel);

// Every frame:
boom.Update(dt, MyVector(0.0f, -4.0f, 0.0f));           // optional gravity; third argument is drag (default 0.8 per second)

// Draw: the effect has no renderer dependency, so use whatever cube your renderer has.
for (size_t i = 0; i < boom.Capacity(); ++i)
{
    const MyCubeParticle& p = boom.At(i);
    if (p.life <= 0.0f) continue;                       // free slot
    const float edge = p.size * MyCubeExplosion::Fade(p);        // 1 at birth -> 0 at death
    DrawCube(p.pos, p.orient, edge, p.r, p.g, p.b);              // p.orient is a MyQuaternion
}
```

`AliveCount()` returns the number of live particles; `Clear()` kills them all (use it when restarting a level). Scramble (scene 9) uses it for every destroyed target.

## VectorShapes - reusable meshes and shape builders (`VectorShapes.h/.cpp`)

A **mesh** is a vertex list plus convex, flat-shaded faces with outward normals. Nothing in this library draws anything: you build the meshes once at start-up and draw them with your own transform and rasteriser (the demo uses `SubmitMesh`). All names live in `namespace MyShapes`.

```cpp
#include "VectorShapes.h"
using namespace MyShapes;

Mesh cube  = BuildCubeMesh();                                // unit cube
Mesh rock  = BuildRockMesh(1, 0xA11CE5u, 0.34f);             // subdivision, random seed, lumpiness
Mesh ship  = BuildShipMesh();                                // fighter: nose +X, wings +/-Z, ~1 unit long
Mesh fleet[kFlCount];  BuildFleet(fleet);                    // the fifteen hand-modelled craft, indexed by FleetId (kFlDart ... kFlStarhawk)
CreatureSet monsters;  BuildCreatures(monsters);             // grunt, ghost, demon, death, skeleton, spider, sorcerer (body + limbs)
```

**Types.** `Face` (vertex indices, normal, `tone` brightness multiplier, `fixedColor` + `color` for own paint such as glass or metal, `glow` for self-lit faces), `Mesh` (`verts`, `faces`, `radius`), `Creature` / `CreatureSet` (articulated monsters: a body mesh plus arm / leg / wing meshes that hinge at their own origin, with hinge positions and animation rates), `PlanetPalette` + `kPlanetPal[7]`, the `FleetId` enum and the inline `Shade(colour, k)` brightness helper.

**Building your own mesh.** Push vertices into `mesh.verts`, then add faces with `AddFaceAuto(mesh, {i0, i1, i2, ...}, interiorPoint, tone, fixedColor, colour, glow)` - the normal is worked out from the first three vertices and flipped to point away from `interiorPoint`, so you never write normals by hand. `AddBoxFaces`, `AddBar` (a tapered diagonal bar: limbs, horns, tails), `AddTaper` (a box narrowing along Y: torsos, necks) and `AddMembrane` (a double-sided wing panel) cover most shapes; `MarkGlow(mesh, n, colour)` makes the last *n* faces self-lit. Finish a hand-assembled mesh with `FinalizeMesh(mesh)` (it stores the bounding radius the renderer uses for culling and level of detail); every `Build...Mesh` function returns a finished mesh.

| Group | Functions |
|---|---|
| Tools | `FinalizeMesh`, `AddFaceAuto`, `AddFaceVec`, `AddBoxFaces`, `MarkGlow`, `BuildIcoSphere`, `AddBar`, `AddTaper`, `AddMembrane`, `FleetBar`, `FleetPanel`, `FleetSlab` |
| Colour / noise | `Shade`, `LerpColor`, `Hash3`, `ValueNoise3`, `Fbm3`, `PlanetPaint` |
| Primitives | `BuildCubeMesh`, `BuildOctahedronMesh`, `BuildDiscMesh`, `BuildBallMesh`, `BuildPuffMesh`, `BuildArrowMesh`, `BuildRockMesh`, `BuildVoxelMesh` (a mesh from an ASCII picture) |
| Spacecraft and hardware | `BuildShipMesh`, `BuildFlameMesh`, `BuildRocketMesh`, `BuildSaucerMesh`, `BuildRaiderMesh`, `BuildWarshipMesh`, `BuildBombMesh`, `BuildFuelTankMesh`, `BuildTurretMesh`, `BuildTieMesh`, `BuildFleet` |
| Characters and props | `BuildMutantMesh`, `BuildHumanMesh`, `BuildGhostMesh`, `BuildHumBodyMesh` / `BuildHumArmMesh` / `BuildHumLegMesh` / `BuildGunMesh` (the articulated person), `BuildPacHalfMesh`, `BuildMarbleMesh`, `BuildSpikeBallMesh`, `BuildRobotMesh`, `BuildDemonMesh`, `BuildGeneratorMesh`, `BuildChestMesh`, `BuildDeathMesh`, `BuildCreatures` |
| Planets | `BuildPlanetMesh(subdiv, terrain)` (procedural; fills a `PlanetTerrain` so `PaintPlanet(mesh, terrain, palette)` can repaint it; subdiv 4 = 5120 faces, 5 = 20480), `BuildEarthMappedMesh(mesh, mapFile, subdiv)`, `BuildMappedPlanetMesh(mesh, mapFile, subdiv, kind)`, `BuildCloudMesh(subdiv, radius, coverage)`, `BuildHaloMesh(subdiv, radius, colour)`, `Fbm3N(p, octaves)` |

Every function is documented in `VectorShapes.h` (what it builds, its size and orientation, which colours are fixed and which take the instance colour). The map-based builders take the **file path as a parameter** (`AssetsDir / "planets" / "earth_hi.rgb"` in the demo) so the library never depends on the project's asset folder. Where `main.cpp` keeps game state next to a builder - the Gyrospace planet table in `BuildGyrospacePlanets`, the creature set `g_creatures` and its short aliases `g_cGhost` ... - it is only the *storage*; the geometry code is in the library.

## VectorRadar - reusable radar widgets (`VectorRadar.h/.cpp`)

Three small value-object widgets draw every radar in the demo. They never call a renderer directly: they draw through the three-function interface `MyRadarCanvas` (`FillRect`, `FillPoly`, `DrawLabel`). Implement it once for your renderer - this is the whole adapter the demo uses (`RendererRadarCanvas` in `main.cpp`):

```cpp
struct RendererRadarCanvas : MyRadarCanvas
{
    Renderer& r;
    explicit RendererRadarCanvas(Renderer& renderer) : r(renderer) {}
    void FillRect(float x, float y, float w, float h, const MyColor& c) override { r.DrawRectangle(Vector2(x, y), Vector2(w, h), c, true); }
    void FillPoly(const Vector2* p, int n, const MyColor& c) override { r.DrawFilledPolygon(p, n, c); }
    void DrawLabel(const wchar_t* text, float x, float y, float size, const MyColor& c) override { r.DrawMyText(std::wstring(text), Vector2(x, y), c, size); }
};
```

Widgets are built on the stack inside the HUD function and cost nothing to create; there is no per-frame allocation.

| Widget | Used by | What it draws |
|---|---|---|
| `MyStripRadar(canvas, x, y, w, h)` | Guardian, Resogun (Voxel Guard) | A camera-centred strip of a **wrapped world**. `DrawFrame()`, `DotWrapped(worldX, cameraX, worldSize, colour, size)` (the shortest way round the ring is the one shown), `ViewBox(fraction, colour)` (the box showing what the camera sees: visible width / world size). |
| `MyScopeRadar(canvas, cx, cy, radius, range)` | Morpheous | A round, north-up scope centred on the player. `DrawFace()` (rim, scope, half-range ring, cross-hair), `DrawSweep(angleDeg)`, `Blip(dx, dz, size, colour)` (world offset from the player; beyond `range` the blip is pinned to the rim, dimmed), `DrawHeading(aimX, aimZ)`, `DrawLabel(text, screenH)`. |
| `MyGridRadar(canvas, x, y, w, h, mapTiles)` | Dominion | A mini-map of a tile map. `BuildRuns(runs, cells, cellColourFn)` reduces the map to merged horizontal runs (a colour with alpha 0 = unexplored, left black) - call it only when the map changes; `DrawFrame()`, `DrawRuns(runs, cells)`, `MarkRect(tx, tz, sizeTiles, colour)` (a building), `MarkPoint(x, z, sizePx, colour)` (a unit), `ViewBox(x0, x1, z0, z1, colour)` (the camera). World +z is up the screen. |

```cpp
// A strip radar, one frame (this is Guardian's HUD, shortened):
RendererRadarCanvas canvas(r);
const MyStripRadar radar(canvas, W * 0.30f, H * 0.030f, W * 0.40f, H * 0.032f);   // above the score digits
radar.DrawFrame();
for (const Lander& l : g.landers) if (l.alive) radar.DotWrapped(l.x, g.camX, kDefWorld, MyColor(255, 70, 70, 255), 4.0f);
radar.ViewBox((g.field.x1 - g.field.x0) / kDefWorld, MyColor(255, 255, 255, 150));
```

## The 3D vector demo (`main.cpp`)

With `__USE_VECTOR_DEMO__` defined the program boots straight into twenty-three scenes: five vector demonstrations and eighteen playable arcade games. Everything is rendered as **filled, lit, depth-sorted 3D polygons** through `Renderer::DrawFilledPolygon`, so it is identical on all four pipelines.

| Scene | Shows |
|---|---|
| 1 Flying Vectors | 16 objects flying at different speeds along curving paths. Velocity arrows (pulsing, hue-shifting), tumbling cubes and octahedra, and basis-vector frames. Each arrow points along its velocity. Reflection off the six planes of the view frustum keeps every object inside the display area. |
| 2 Vector Field | Ring of arrows rotated with one `RotateAroundBatch` call. |
| 3 Cross / Dot / Project | `Cross`, `Dot`, `Project`, `Angle`, `SignedAngle` between two moving vectors. |
| 4 Reflect / Project | `Reflect`, `ProjectOnPlane` and `FromToRotation` on a tilting slab. |
| 5 Slerp / Quaternions | `Slerp` vs `Lerp`, quaternion `Slerp` and `RotateTowards` on cubes. |
| 6-23 | The arcade games, described below. |

Keys: **1-9 and 0** pick scenes 1-10, **F1-F12** pick scenes 11-22 (F12 is the Dominion RTS; Project X is scene 23: Ctrl+F1), **Page Up / Page Down** go to the previous / next scene (all of them disable auto-cycle), **P** pause (Space also pauses in scenes 1-5), **C** toggle auto-cycle, **Esc** quit. Auto-cycle only walks scenes 1-5; the games are never switched away mid-play.

### The arcade games (scenes 6-23)

**Wave banner.** A new wave announces itself with a small animated vector "WAVE n" low on the screen (the same stroke lettering as GAME OVER, now with W and digits): the letters drop in one after another, bob and swing, and after about 1.7 s the whole word scrolls off to the right (odd waves) or left (even waves), moving faster and faster until it is gone (distance grows with the square of the time; `g_banner`, `g_bannerDir`, `DrawGameHud`).

**Get Ready cue.** Whenever the game says "Get ready, Player 1" (after a life is lost, once the ship is about to return, and when a new game starts after a game over) a vector **GET READY** banner crosses the middle of the screen in the game's own colour: it scrolls in from the right, **starting fast and slowing to a stop exactly at the centre** (cubic ease-out, 0.85 s), **holds for 1.5 s**, then scrolls out to the left, **starting slowly and speeding up** (cubic ease-in, 0.85 s). The banner is shown even with TTS switched off, never in demo mode, and is cancelled if you switch game (`g_getReady`, `AnnounceGetReady`, `DrawGameHud`; the timings are the `kGetReady...` constants). The spoken line is a sultry female voice: `SpeakGetReady` picks the best installed female SAPI voice (Zira, Eva, Aria, Jenny, then Hazel, Susan, Linda, Catherine ... in that order of preference), drops the pitch slightly (-1.5) for a huskier tone and speaks briskly (rate +1; the old male line was -2). With no female voice installed it uses the default voice pitched up (+3). To add more voices on Windows, install them under Settings > Time & language > Speech.

**Demo (attract) mode.** Every game starts in demo mode whenever you switch to it, and an autopilot plays it with real tactics so you can see how the game is played. **No sound effects are made**, no score or wave is recorded and there is no GAME OVER banner - the run restarts when it ends or after 75 seconds. A flashing, animated vector-style **DEMO** banner (the GAME OVER lettering, cycling colour) and a blinking PRESS FIRE TO START prompt are drawn on top. **Fire / Enter** (after the button has been released once, so a key held to get here does not count) ends the demo and starts a fresh, scored game. The autopilots (`Bot...` functions in `main.cpp`, dispatched by `DemoBot`):

| Game | What the demo does |
|---|---|
| Asteroids, Scoptiliptic | Faces the nearest target and fires only when lined up; turns tail and thrusts away when a rock / mine / chaser comes within about 3 units; shoots the saucer first. |
| Guardian, Resogun | Catches falling humans - up to **three** at once (it keeps catching the nearest falling one, then dives and lands them all) - hunts abductors that carry a captive first, matches height and faces the target before firing, smart-bombs when surrounded. |
| Scramble | Flies a safe height above the terrain ahead and below the canyon roof, fires constantly, matches the height of air targets, bombs fuel tanks and turrets as they pass under the nose. It also **evades**: every turret / bandit shot (and each bandit, diver and saucer body) is projected to its closest approach relative to the ship and the ship sidesteps away from where it would pass; for each rocket (launched, or about to launch) it predicts the band of heights the climbing rocket will fill when it crosses the ship, and if its planned height is in that band it flies over or under it, whichever the ground / roof allows and it can reach in time. |
| Chomper, Chomper 3D | Breadth-first search over the maze: eats the nearest pellet, avoids tiles near chasing ghosts, goes for a power pellet when a ghost is close, then chases the blue ghosts while the power-up lasts. When a chasing ghost charges straight at him down a corridor he keeps running and presses jump (X) so he hops over it (`PacDemoHopAhead` / `PacDemoJump`). |
| Neon Arena | Kites: backs away from the crowd while circling it, avoids the walls, aims at the nearest foe, bombs when surrounded. |
| Morpheous | Keeps about 14 units from the nearest enemy and circle-strafes it, steers round asteroids, dodges bolts, collects power-up pods when it is quiet. |
| Project X | Always fires, dodges bolts, collects power-up capsules, otherwise lines up with the nearest ship ahead. It keeps 1.5 clear of the (deadly) floor and roof using a look-ahead of the highest ground / lowest roof, sizes its dodge of a ship by that ship's hit radius (bosses are big), and flies above a flame vent that is warming up or erupting. |
| Dungeon | Fetches health when hurt, grabs close chests, then hunts the monster generators along the game's own path field and shoots monsters in range; takes the stairs when none are left. |
| Marble Madness | `BotMarble` is a **planner**: every 0.1 s it runs the game's own marble physics (slope gravity, friction, speed cap, jump arc, 0.2-tall landing window) and the enemies' movement forward ~2 s under about 70 candidate plans (a constant stick, plus "jump after 0 / 0.1 / 0.2 s" or no jump) and flies the best one. Dying scores far below anything else (dying later beats dying sooner); otherwise it rewards progress and staying near the track centre, **rewards stomping** an enemy (landing on its head while falling), and penalises needless jumps and close shaves. So with no special cases it brakes before a hole if its jump would overshoot into the next, times take-off to land on solid floor, sidesteps or out-runs chasers, jumps spike rollers, does not jump at hoppers / bombers unless it can come down on them (the game kills on touch at any height except for rollers), keeps clear of the colossus and out of the bomber's blast. Floors are tested with a 0.2 border and enemy / acid radii get +0.2 to cover the stick filter's lag. |
| Trench Run | `BotTrench`: picks a target point for the ship, bends it around dangers, then steers to it. **Battle:** finds the most urgent TIE it can reach and flies to where that TIE will be when the laser arrives (the TIEs' weave and their screen-wide convergence are predicted). **Surface:** lines up with the nearest tower (turrets first) and climbs over any tower about to be rammed. **Trench:** the nearest beam on each axis decides where the ship may be (low bar: fly high, high bar: fly low, half-wall: the other side) until it has passed; it shoots wall turrets while the port is far off, then centres on the exhaust port and fires the **torpedo** once lined up (port 6-60 units ahead, \|x\| < 0.4, relaxed to 0.75 in the last 25 units). **Always:** enemy bolts and ramming TIEs are projected to the ship's plane and the target steps to the nearest clear spot. |
| Spiral Tunnel | `BotTunnel`: fire is always held. Each frame it lists the **hazards** that will reach the ship's z within 2.2 s (enemy bolts as straight lines; every ship's predicted position from the game's own movement laws; chargers and pursuers **home**, modelled as a catch-up factor; the spinning gates' bar angle at crossing time; arc blocks; vortexes), picks an **aim point** (a nearby power-up, else the most valuable target in bullet range - chargers, warships, the boss first - led by the bolt's flight time), then scores ~50 candidate positions (a grid, the aim point, staying put, the last choice, and the exact gaps between a gate's bars) by distance from the aim plus a penalty for every hazard the ship would be too close to **when it arrives** (it travels at 7 u/s), and steers to the cheapest. |
| Jumpman | Still the generic wandering autopilot (smooth stick, pulsed fire). |
| Gyrospace | `BotGyrospace`: steers round the rim by ANGLE (shots run along the cone, so a target is hit when the ship's angle matches its). While the drone trio is out and the twin gun is not won it aims for the **centre drone** before anything else (leading its sway); otherwise the most urgent ship (near in angle, near the rim); it sidesteps shots and rammers, always fires, and bombs when six or more ships are out. |
| Dominion | No bot: its enemy AI still attacks and the demo restarts when the base falls. |

The code is `g_demo`, `DemoBot`, `DemoFrame` and `DrawDemoOverlay`; `PlaySfx` / `PlaySfxLoop` return at once while `g_demo` is set.

**Rescuing in Guardian and Voxel Guard.** The ship can hold up to three caught humans at once (`kMaxCarried`; they hang side by side below it, at the offsets in `kCarryOff`). Each is saved the moment its feet touch the ground, so you can catch several falling people and then land the lot. The old rule allowed only one at a time.

**Demo length and the hall of fame.** A demo run lasts until the autopilot's game is over or `kDemoRunSecs` (120 s; `kDemoRunSecsRts` = 240 s for Dominion) has passed. It then does not restart at once: `DemoFrame` sets `g_demoHsT` and `DrawHallOfFame` shows that game's **TOP 5** (23 s in all, `g_demoHsEnd`) while the game behind it stays frozen (Overlay passes it `dt = 0`):

* the screen dims, stars twinkle and a neon floor grid drifts; "TOP 5" flips open letter by letter in cycling colours;
* the five lines **drop in from the top of the screen** and bounce to rest, fifth place first and the champion last (a falling line never crosses one that has landed); each flashes and gets a travelling highlight, and first place throws a ring of sparks;
* the finished table is held for **15 s** (`kDemoHsOutDemo`), then **every line scrolls off in its own random order and direction** (left, right or up, accelerating; `g_demoHsSeed` is picked per showing), after which the **whole screen fades to solid black** (the last `kDemoFadeOutSecs`, a black sheet drawn over everything);
* the demo then restarts from the beginning. FIRE / ENTER at any point starts the real game instead.

While the screen is solid black `DemoFrame` resets the demo, and the new run **fades in from black** (`g_demoFade`, a black sheet thinning over `kDemoFadeInSecs`, drawn last by `DrawDemoOverlay` so the HUD and banner fade in too).

The lettering is `DrawHallText`: the banner font's strokes drawn **solid and bold** - thick filled strokes with square caps that overlap at corners, over a dark outline that separates letters from the grid and from each other, with a lighter bevel line and (on large text) a soft glow. All spacing comes from one constant, `kHallAdv` (a letter is `0.80 x` its height across), and every layout derives its text height from the screen height *and* width, with the table headings 2.1 text-heights above the first line, so nothing can overlap. The dimming layer is nearly opaque so the frozen game's HUD cannot show through. It is only drawn while the table is up, when the game is frozen.

**New high score.** When a real game ends with a top-5 score, the game pauses and `DrawHighScoreEntry` (drawn last by `Overlay`, using the same tubing lettering and backdrop) replaces the old plain-text layout. It is built from separate horizontal bands so no label touches another: the title "NEW HIGH SCORE", the score, "YOU TOOK 2ND PLACE", "ENTER YOUR NAME", ten name slots (a typed letter, or the blinking letter the pad is scrolling, in the first free slot) and two pairs of help lines (keyboard, pad). `NameEntryUpdate` still reads the keys and the pad. When the name is confirmed, `StartHallAfterEntry` turns demo mode on and shows the hall of fame with the new line highlighted (rainbow pulse and a NEW tag, held 8.5 s, `kDemoHsOutEntry`); the lines then scroll out at random and the demo restarts. The run was committed by `SubmitName`, so it is never entered twice.

All five are playable with a **joystick or the keyboard**, and all share the same framework:

| Scene | Game | What it shows |
|---|---|---|
| 6 | **Asteroids** | Rotate, thrust and fire a modelled fighter ship in a wrap-around arena. The rocks are weathered, faceted stone (lumpy icosphere meshes with per-face tones) that split large, medium, small and tumble on X, Y and Z. Every so often a **flying saucer** crosses the screen and shoots at you: your shots bring it down for 200 points, and **an asteroid that hits it destroys it**. The same goes for every special raider that arrives from wave 2: any enemy ship that touches a rock is destroyed, and the ships are **evasive** - they swerve around rocks in their path (more sharply the closer and faster the rock is) instead of flying straight into them. Other games using the raiders (`SrCtx`) are unaffected: the `avoid` / `touches` hooks are optional. Each wave adds a rock and speeds them up. |
| 7 | **Guardian** | Side-scrolling wrap-around world with ridge terrain and a radar strip. Landers are domed flying saucers with legs and rim lights; they abduct humanoid people and mutate into spiky tumbling urchins if they escape. The people are animated: standing people **wave** or stroll about, a person being carried off waves for help, and a falling person flails. Shoot a carrying lander and **catch the falling human** (a fall starts gently and speeds up steadily): it **clings to your ship until you land it**, and once its feet touch the ground it is saved, scores the bonus and **jumps for joy**. If your ship is destroyed while carrying someone, they fall again. Smart bombs clear the screen. |
| 8 | **Scoptiliptic** | Inertial ship in a walled arena around a solid centre block. The walls are rubber: ship, droids **and your shots** ricochet using `MyVector::Reflect`. Droids lay mines; if you dawdle, a death-ship hunts you with `MyVector::RotateTowards`. |
| 9 | **Scramble** | Auto-scrolling mountains; a wave is 450 m. Shoot ahead, **bomb the ground**, mind the fuel gauge (shoot fuel tanks to refuel). Ground rockets are modelled (banded body, nose cone, fins) and climb straight up with an exhaust flame and a cube smoke trail, swaying only about X. Every kill is a **cube-particle explosion** (`MyCubeExplosion`). Shots (and bombs) cannot pass through the ground: they burst against the terrain. |
| 10 | **Chomper** | A freshly generated maze every wave (carved spanning tree, loops added, dead ends removed). Ghosts are real ghost models (domed head, scalloped hem, eyes) that float and turn to face the way they travel; their chase targets are computed with `MyVector` distances and power pellets turn them blue. Chomper is a fan polygon turned with `RotateAroundBatch`. |
| 11 | **Planet Assault** (A) | *Super Stardust* style. The ship hovers over the north pole of a 3D planet and never moves: the stick **rolls the planet underneath it**. Drones home in, orbiters circle the world on great circles, spiralers corkscrew in, bombers are tough and split into drones. The planet is a big procedural world: noise-driven continents with real relief (mountains stand proud of the sea), deep ocean, shallows, beaches, grassland, desert, forest, bare rock, snow-capped peaks and polar ice, with a drifting translucent cloud layer and an atmosphere glow round the limb. Positions are unit vectors in planet space; shots follow great circles (`RotatedAround`, `RotateTowards`). |
| 12 | **Neon Arena** (B) | *Geometry Wars* style twin-stick shooter. **Every wave picks a random line-up of enemy types**, and each type behaves differently: wanderers ricochet off the walls (`Reflect`), seekers are turn-limited homing missiles (`RotateTowards`), weavers charge in a sine wave, dodgers side-step your bullets, splitters burst into minis, and gravity wells drag you and the other enemies in. Score multiplier, bombs, neon grid. |
| 13 | **Trench Run** (C) | Vector *Star Wars*. A TIE-fighter battle, then skim the Death Star surface (towers and turrets), then dive into the trench: dodge the beams, shoot the wall turrets, and fire **one proton torpedo into the exhaust port**. A hit blows up the Death Star in a huge cube explosion and the next attack is harder; a miss costs a life and you fly the trench again. |
| 14 | **Voxel Guard** (D) | *Resogun* style, on a **cylinder world** (everything curves away at the screen edges). Voxel enemies: abductors steal humans, bombers carpet-bomb the ground, swarmers kamikaze, guardians are armoured gunships, and an abductor that escapes mutates. Voxel people wave, stroll and wave for help; catch a falling human and it clings to your ship (**up to three at once**, hanging side by side - `kMaxCarried`) until you land them, then each jumps up and down (a bonus). Every human alive at the end of a wave is worth a bonus too. The ground and the tower are set low on the screen (`groundTop = -6.5`) to leave more room to fly and dodge. The demo catches up to three falling humans before it dives to land them. Humans are always drawn in full: `SubmitMesh(..., keepShape = true)` stops their limbs turning into hexagon blobs at low Vector Quality. |
| 15 | **Chomper 3D** (E) | The classic chase from a **chase camera inside a 3D maze** (a high, zoomed-out camera that swings round behind Chomper's heading so more of the maze is visible). Chomper is a sphere whose two halves hinge open and shut; the ghosts are 3D ghosts. Shares the maze generator, ghost AI and scoring of scene 10. **Steering is relative to where Chomper faces** (the camera turns with him): up keeps going, left / right turn (queued for the next junction, not repeated while held), down turns around. |
| 16 (F6) | **Marble Madness** | A random 3D hilly track: flat runs, slopes with banking, holes (one or two rows long) and acid pools. The marble rolls with slope gravity and friction (it visibly rolls: swirl-banded icosphere turned about `up x velocity`), and **Fire jumps** over holes and pools. Falling off an edge, acid, a black chasing marble or a bouncing green hopper kills you; land on an enemy to squash it. Reach the flag before the clock runs out, checkpoints every 12 rows; each track is 8 rows longer than the last (up to about 400 rows) and narrower and busier, and the clock grows with it (25 s plus roughly a second per row, tightening slightly each level). |
| 17 (F7) | **Jumpman** | Single-screen platformer: defuse every bomb while climbing ladders and ropes (the articulated human has run, climb and jump poses). Patrolling robots (jump them), wall turrets that fire along your floor when you stand on it, falling rocks with a flashing warning marker, and a fall of more than one floor is fatal. Bombs float over the gaps too. **Every level is built randomly** (ladder and rope columns, the number, position and width of the gaps, bombs - some hanging over the gaps - robots, turret floors and sides, and the colour scheme all change) and **gets harder**: more and wider gaps, ladders further apart, more bombs, more and faster robots, up to three wall turrets and four rock droppers. |
| 18 (F8) | **Gyrospace** | You orbit the rim (stick = along the rim) and shoot at the vanishing point. Waves spiral out of the distance on **random flight paths** (spiral turns, wobble, mirrored halves), orbit the rim shooting, then dive away. Four warps per planet, **Pluto > Neptune > Uranus > Saturn > Jupiter > Mars > Earth** (the planet in the background grows each warp), then the cycle repeats faster. The 4th warp is an armoured warship squadron. On harder waves some ships come up from the **rear**: they start far behind the player (60 units behind the ring, well behind the camera, so out of sight), rush in over about 3 s, slow as they come into view and take up their orbit; they do not fire until they are on screen (`kGyRearR`, `kGyRearTime`). Smart bomb on B. |
| 19 (F9) | **Dungeon** | Gauntlet style: random mazes (depth-first carve plus extra openings). Throw axes at ghosts (fastest), grunts and fireball-throwing demons (slowest). **No monster can pass through a wall**: they find their way round the maze with a breadth-first flood fill from your tile (rebuilt only when you step onto a new tile, read by every monster). From depth 3 **DEATH** - a tall dark hooded human with glowing red eyes and a scythe, in a blood-red aura - walks in from far away: **one touch is instant death**; he takes six axe hits; bone-pile generators keep spawning them until destroyed. Health drains constantly: eat food, take treasure, find the **stairs down** (bigger maze, more generators each depth). |
| 20 (F10) | **Spiral Tunnel** | You fly down an endless winding tunnel built from rings of panels with a spiral of colour and a helical twist, bending left/right/up/down. Drones weave, interceptors circle the wall shooting, mines sit on the wall, chargers home in, warships are armoured, and spinning barrier gates must be flown through the gap. Faster each wave. (F10 is normally a Windows system key; the window procedure swallows it.) |
| 21 (F11) | **Morpheous** | A **top-down** space shooter over a huge 280 x 280 arena (the camera follows you; move with the stick, aim with the right stick / arrows, otherwise you shoot the way you fly). A **RADAR** at the right of the screen shows where every ship, asteroid and power pod is - anything out of range is pinned to the rim. Waves of 5 to 11 formations arrive from every direction: lines, V formations, rings that close in around you, clusters, pincers from two opposite sides, drifting asteroid fields and armoured warships; ships blow up if they hit an asteroid. Carriers drop pods: **gold pods build the ship up** through 6 stages (wing pods, twin cannons, engine nacelles, nose armour, outer wings, orbiting option drones) while its fire grows from one shot to a five-way spread plus options; blue pods give shields. A hit costs a life and strips two build stages. |
| 22 (F12) | **Dominion** | Real-time strategy (see the Dominion section below). |
| 23 (**Ctrl+F1**) | **Project X** | Side-scrolling shoot-'em-up. Each wave picks a random flight path (sine, loop, dive, zig-zag, circle - all gentle, rounded curves; ships swing round through wide arcs rather than snapping) and enemy kinds are added as you progress (up to seven, one of them the demon). Red carriers drop capsules: speed (5 levels), weapon (single > double > spread > laser), rear gun, shield and up to two option drones, until the ship is maxed. Every fourth wave is a boss; beating it starts the next level. Dying strips a weapon level. |

Controls (every game):

| Action | Joystick | Keyboard |
|---|---|---|
| Move / steer | Stick or hat (D-pad) | Arrow keys or WASD |
| Fire | Button 1 (or 3) | Space or Ctrl |
| Aim (twin-stick games 11, 12) | Right stick (Z / R axes) | Arrow keys (WASD only moves) |
| Alt (thrust, smart bomb, bomb) | Button 2 (or 4) | B, X or Shift |
| Restart after GAME OVER | Fire or button 8 | Space or Enter |

The first controller found by `winmm` (ID 0 or 1) is used; its stick, hat and buttons are merged with the keyboard (whichever pushes harder wins). Controllers are only probed when the engine's `Joystick::HasActiveControllers()` is true, so having no pad costs nothing. Plug one in and it is picked up automatically (the engine rescans every 3 seconds); the HUD says which device is active.

**Sound effects.** Twenty-nine WAVs from `Assets/` are played through a small preallocated voice pool:

| File | Played when |
|---|---|
| `firing.wav` | the player's gun fires (every game) |
| `destroyed.wav` | the player's ship is destroyed |
| `enemy-explosion.wav` | an enemy craft (or a rock) is destroyed |
| `whistle.wav` | Chomper eats a power pellet |
| `enemy-fire.wav` | enemy ships, turrets, wall guns and demons shoot (throttled, quieter than your gun) |
| `powerup.wav` | pods, capsules, food / treasure and bombs defused are collected |
| `jump.wav` | the marble or Jumpman jumps |
| `crunch.wav` | a generator is destroyed or an enemy is squashed |
| `boss-explosion.wav` | a Project X boss dies |
| `warning.wav` | a boss is approaching |
| `stairs.wav` | stairs down, the Marble Madness flag, a Jumpman level or a Gyrospace planet is cleared |

| `gn-axe.wav` | Dungeon: the hero throws an axe (a knife whoosh) |
| `gn-hit.wav` | an axe thuds into a monster |
| `gn-genhit.wav` | an axe clangs on a monster generator (destroyed ones also crunch) |
| `gn-hurt.wav` | the hero takes damage (throttled) |
| `gn-food.wav` / `gn-coin.wav` | food is eaten / treasure is picked up |
| `gn-step1.wav` / `gn-step2.wav` | footsteps while the hero walks (they alternate) |
| `gn-stairs.wav` | down the dungeon stairs (a door opening) |
| `gn-bell.wav` | a Death Reaper arrives: a tolling bell, with the warning sound |

| `gn-ghost.wav` / `gn-grunt.wav` / `gn-demon.wav` / `gn-skel.wav` / `gn-spider.wav` / `gn-sorcdie.wav` | each Dungeon monster's own death voice (a wail, a growl, a roar - also used for the Reaper -, a bone clatter, a skitter and squeal, a falling zap) |
| `gn-sorc.wav` | a sorcerer casts its three magic bolts |

The `gn-*` monster voices are generated in code (not sampled); everything else in this table is a converted CC0 recording. The seven before the Dungeon ones are converted from Kenney's CC0 "Sci-Fi Sounds" pack (see `Assets/KENNEY-SCI-FI-SOUNDS-LICENSE.txt`).
| `whistle.wav` | Chomper (2D and 3D) eats a power pellet; it is stopped when the power-up ends, when Chomper is caught, or when you leave the scene |

Each file is decoded once and gets a few DirectSound voices; extra voices are `DuplicateSoundBuffer` copies that share the sample data, so playing a sound is "restart a free voice": nothing is allocated or copied during play, nothing leaks, and rapid fire and chain explosions overlap cleanly (the oldest voice is stolen when all are busy). Rapid retriggers are throttled, the 10-second explosion sample is cut off after 2.6 seconds, and the volume follows the engine's SFX volume (`dialogVolume`). A missing file just stays silent. In code, `PlaySfx(kSfxFire)`, `PlaySfx(kSfxPlayerDown)`, `PlaySfx(kSfxEnemyDown)`, `PlaySfx(kSfxWhistle)` / `StopSfx(kSfxWhistle)`; `Boom()` plays the right explosion automatically (the player's ship always explodes in colour `(240,240,255)`).

**Scramble turrets and canyon.** The anti-air turrets keep their base fixed while the gun swings round (150 degrees per second) to follow your ship. When it is lined up and you are on screen it fires a glowing shell at a random interval; shells die on the ground and kill you on contact, and later waves fire faster. From wave 3 a rock roof eases in over 400 m and gets lower each wave, turning the level into a canyon: touching it kills you, your shots and missiles burst against it, saucers keep below it, and the passage is always at least 3.8 units tall.

**Longer, less predictable waves.** In the shoot-'em-ups waves are now much longer and ships no longer only come from ahead: Project X groups enter from the right, from BEHIND on the left, from the top and from the bottom (some groups alternate between two opposite sides) with five random flight paths; Morpheous formations surround you; Scramble sends bandits that overtake you from behind (wave 2 on) and divers that drop from the top of the screen or out of the canyon roof (wave 3 on) - never from the ground, and never through rock; the spiral tunnel sends pursuers up from behind; Gyrospace waves are runs of 2-5 sub-waves that, as the waves go on, start less and less often from the centre (on the rim, part-way out, or diving in from the rim); Guardian's waves are bigger. The spiral tunnel also **spins** - at a new random speed and direction each wave.

**Later updates.**
- **Morpheous** is zoomed out much further and the radar covers 80 units. Formations are centred on random spots of the arena at least 34 units from you and are spread out far more. Enemies **idle until you get close** (an aggro radius that grows with the wave, 14 to 26 units; being shot also wakes them) and lose interest when you get well away. The higher the wave, the more often and the faster they shoot (from wave 6 every ship fires a pair).
- **Dungeon** (Gauntlet): mazes start at 20 x 20 cells and grow to 36 x 36, the camera is zoomed well out, and there are three new minions: **skeletons** (sword and shield), **spiders** (fast crawlers) and **sorcerers** (keep their distance and cast a fan of three magic bolts). New kinds join as the depth grows. Monsters move faster and the generators spit out several at once, and faster, the deeper you go. **When you die every Death Reaper vanishes** until it is summoned again (30-45 s later); at most 1 at depth 3, 2 from depth 4 and the full 3 only from depth 8.
- **Chomper and Chomper 3D:** the maze grows every level (21 x 15 tiles on level 1, 6 wider and 4 taller each level, up to 71 x 49). **Power pellets are more numerous** on the harder levels (4 on level 1, 2 more each level, up to 20) but **last less**: 10 seconds on level 1, one second less per level, never under 2. A **bonus fruit** (cherry, strawberry, orange, apple, melon, Galaxian flagship, bell, key - worth 100 to 5000) appears at the start tile after 30% and again after 70% of the dots have been eaten, and stays for 10 seconds. The 3D camera never turns by itself now - only B turns it - and the stick is screen-relative.
- **Chomper sounds** (generated, not sampled - see below): the two-note chomp while eating, the death warble, the power-pellet arpeggio, eating a ghost, the fruit jingle, and the two loops: the **siren** while the ghosts hunt you and a faster warble while they are blue.
- **Marble Madness track shape:** track 1 has the widest floor and the tallest hills; every later track narrows the floor (down to a minimum) and flattens the hills (down to about a third), so the course gets harder to stay on but easier to read.
- **Marble Madness:** up to 14 enemies at once and they arrive faster. New kinds are unlocked by the number of tracks cleared: after 10 tracks the purple **Shadow marbles** (fast, weaving) and **Spike rollers** (they roll down the track at you: jump over them); after 20 the **Wraiths** (float over holes) and **Bombers** (stop, flash, then blow up - jump when they flash); after 30 the huge **Colossus**. **Mutual destruction:** an enemy that kills the marble on contact is destroyed in the same impact (small explosion, no points) instead of lingering over the marble's respawn delay; this applies to every kind and in demo mode too.
- **Gyrospace:** on the harder waves ships come up **from the rear** (from the camera side, through the ring) and some ships **fly right past you** after their orbit, passing through your ring before they leave the screen, so they can hit you.
  From wave 2 a sub-wave may also fly as a **formation** (chance 29 % rising to 60 %, `GySpawnWave` modes 5-9): a **rear wedge** (a V rushing in from behind), a **rear ring** (closing on you from behind),
  a **pincer** (half from behind on the opposite side, half charging at you), a **charge column** (single file straight at you down one lane) and a **charge fan** (a wall of ships rushing across the rim at you).
  The ship's orbit ring is larger: `kGyR` 5.4 (was 4.6) with the camera 15 behind it (was 14); `kGyK = kGyR / 4.6` scales the enemy orbit radii, the fly-by and charge reach so their paths keep their shape.
  **Drones, twin gun, tougher ships** (`GyDrone`, `GyStartDrones`, `GyGame::twin / ringT`): from wave 2 the first sub-wave of a warp may bring a trio of **drones** (60 % chance - and **never while you hold the twin gun** (or its ring is on the way): they only come back once the ship has been lost and you are on the single gun again). Each drone is a small reconnaissance satellite with its own mesh, `BuildDroneMesh` in `VectorShapes.cpp` (faceted probe body with a glowing amber sensor lens facing you, a dark equatorial band, a mast with a dish antenna, and two solar wings each with a boom, frame and three blue cells; steel for the outer two, gold for the centre one); about 1.4 across for the outer drones and 1.9 for the centre one. They rise out of the planet in a spiral, then **sway** like the arcade's satellites - drifting to and fro along the rim (55 degrees either way, `GyGame::dBase / dSwayT`) while bobbing slowly in and out - for 8-11 s, firing at you at random times (every 1.2-3.6 s each), then sink back into the planet. **No ship ever appears at mid distance now:** every sub-wave rises out of the planet at the centre, comes up from far behind you, or flies as a rear / charge formation (the old "appear on the rim / part-way out" start places are no longer chosen). The rise does not start at the exact centre: each sub-wave picks a **random point on the planet's disc, never its dead centre** (`GyEnemy::ox / oy`: 2.0 to 3.0+ units from the planet's centre, further out on later warps, plus a 0.7 per-ship scatter) and the offset fades as the ship climbs to its orbit, so you cannot assume where they will come from; the drones do the same (`dOx / dOy`). The way OUT works the same way in reverse: a ship that retreats or dives away heads for its **own random spot on the planet, never the centre** (`GyEnemy::tx / ty`, blended in as its radius shrinks; the drones sink toward `dTx / dTy`). The two outer drones are worth 100; shoot the **centre** one and a **ring effect** plays (`kGyRingTime`, 1.6 s): a ring lies just outside your orbit and two bright highlights leave the spot where the drone died and run round it in **opposite directions**, lighting the ring behind them, and meet **at your ship** (the meeting point follows the ship if it moves). It has sounds: a rising sweep as it starts (`kSfxDfCatch`, Guardian's "caught" sound) and three quick rising beeps plus the power-up chime when the highlights meet (`kSfxDfSaved`, `kSfxPowerup`), after which the ship fires a **twin gun**: two shots side by side along the rim, each doing **2 damage**, with bigger cyan bullets and TWIN GUN in the status line. Losing the ship loses the twin gun; a centre drone shot while armed is worth 1000 instead.
  Ships start **weak** and climb gradually to **hard**, then stop: hp = 1 + (wave - 1) / 2, **capped at 6 hits** (1 on waves 1-2, 3 on wave 5, 5 on wave 9, 6 from wave 11); the armoured warships 3 + wave / 2, capped at 10; drones 2 + wave / 4, capped at 6. A ship that is hit is **outlined** in bright yellow for **0.25 s** (`kGyHitTime`). `GySubmitOutline` draws a 25 % larger copy of the model behind it in the renderer's new FLAT mode (`g_meshFlat` / `MeshFlatScope` in `SubmitMesh`: one bright colour, no lighting, per-face paint ignored - the ships' own fixed paint used to show through and hide the outline) - `GyEnemy::hitT`; the drones' body and panels are outlined too). The smart bomb deals a flat 8 damage to every visible ship. Drones fire slowly at first (7-13 s between shots on wave 2, the interval divided by a rate that grows 0.3 per wave up to 6). A ship coming up from **behind** you cannot hit or shoot you until it has passed in front of your ship's plane. At the end of each planet's four warps the ship **warps into the planet** (3.2 s spiral into the planet with the planet swelling, then a white-out that fades on the next planet; `GyGame::warpT`, `GyFinishWarp`).
  **Score awards** (`GyAwardScoreBonuses`, thresholds `GyGame::nextLife / nextBomb`): an **extra life** at 10,000, at 50,000 and then every 100,000 after that (150,000, 250,000 ...), and a free **bomb** at every 50,000 (lives and bombs top out at 9). A life plays `powerup.wav` (`kSfxPowerup`, the same sound the other games use for an extra life) and a bomb plays `ma-bonus.wav` (`kSfxMiBonus`, a short bonus blip), each with an "EXTRA LIFE" / "NEW BOMB" notice for 2 s. Both sounds are existing entries in the shared sound table (`SfxId`), so a different effect is a one-word change in `GyAwardScoreBonuses`.
  Charging ships use state 6 (`GySpawnWave` / `GameGyrospace`): they leave the vanishing point on a fixed lane aimed at your rim position at spawn time and pass through your ring, so move out of the way.

**Planet Assault: asteroids, weapons and power-ups.**
- **Asteroids** drift over the planet along their own great circles. A whole rock (phase 5) breaks into 2 rocks of phase 4 (3 for the two biggest phases), and so on down to phase 1: **five phases of breaking up**. Smaller pieces drift faster; touching any piece costs a life. Whole rocks are never green; each **fragment** of a breaking rock has a **4 % chance** of being **green** (`StarBreakAsteroid`), a green piece always drops a power-up, and a green piece's own fragments are never green.
- **Weapons:** **R1 / L1** (pad buttons 6 and 5) or **E / Q** switch between the **Flamer** (the old single-shot Blaster, now a flame thrower: about 20 short-lived flame particles a second in a +-3 degree cone, each reaching about 19 degrees of arc in 0.3 s, burning through 3 targets, growing and cooling from white-yellow to dark red; one throttled whoosh every 0.11 s), the **Spread** (3 shots, 5 at power 5+), the piercing **Laser** (passes through 1 + power/3 targets) and the homing **Missile** (it also hurts everything near the hit). Every weapon has its own **fire rate** (level 10 fires about 2.6 times as fast as level 1) and **power** level, both **maxed at 10**; the HUD shows the current weapon and its levels.

**Flame thrower look (Super Stardust).** Each flame particle (shot kind 4) is drawn as stacked glows that change with age: a blue-white hot base at the nozzle, a white-yellow core, a yellow-orange body, an orange-to-deep-red outer tongue and finally dark grey smoke; every particle flickers on its own phase, sways sideways and licks upward off the surface as it cools, and one in three throws off a glowing ember. A flickering blue-white / orange glow sits at the nozzle while firing (`StardustGame::flameT`). The particles also slow down as they spread (speed x (1 - 1.6 dt)), starting at 80 degrees of arc a second so the reach stays about 19 degrees. Cost: up to six small meshes per live particle, about fifteen alive at once, nothing allocated.

**Super Stardust missiles.** The homing missile (weapon 3, shot kind 3) is now drawn as the rocket model (`g_rocket`, via `DcDrawRocket`: octagonal body, white nose cone, fins and a flickering exhaust flame) scaled to 1.02 units (40 % smaller than the first version) and pointing nose-first along its great circle (the direction is `world * (axis x n)`). **It is oriented like a real missile**: the nose (the mesh's +X) points along its velocity (the tangent of its great circle, `world * (axis x n)`), its up is the surface normal tilted toward the inside of the current turn, so it **banks into its homing turns** (`StarShot::bank`, measured from how fast its flight axis swings) and levels out when flying straight; the exhaust flame sits at its tail. It leaves a **trail of sparks**: every frame two orange sparks and one pale-yellow spark are dropped at its exhaust (`g_fx.Sparks`, pooled, skipped when the Video tab's effects are off), and they drift and fade behind it as the planet rolls.

**Super Stardust demo (attract mode) and power-ups.** `BotStardust` now goes and collects **every power-up**: the ship rolls the planet toward the nearest one (a firm pull when nothing is close, a lighter pull added to its dodging when enemies or shots are near), including ones just over the horizon, since a power-up only lasts 10 seconds. It still aims and fires at the nearest enemy on the way and uses a smart bomb when six or more are close.

**Super Stardust asteroid materials.** Whole rocks now come in three materials, and their fragments keep it: **ROCK** (brown, 50 %), **ICE** (pale blue, 30 %) and **GOLD** (yellow, 20 %, from wave 3; before that ice takes its share). Each has a weapon that breaks it easily and others that need more hits (`kStarMatMul`, damage multiplier per weapon):

| Material | Flamer | Spread | Laser | Missile |
|---|---|---|---|---|
| Rock | 0.45 | 0.8 | 0.6 | **1.5** |
| Ice | **1.5** | 0.8 | 0.45 | 0.6 |
| Gold | 0.3 | 0.6 | **1.5** | 0.45 |

So the wrong gun takes roughly 2-3 times as many hits; the Spread is never the best choice; the smart bomb ignores materials. A hit with a weapon below 0.5x plays a dull `sx-shield` clink. Power-up (green) rocks stay green but keep their material's toughness. The table, `StarMatMul` and `StarMatColor` sit just above `StarSpawnAsteroid` in `main.cpp`.

**Hit highlight in Scoptiliptic and Neon Arena (Geometry Wars style).** The same highlight now applies to every enemy in both games: when a shot, missile blast, mine blast or smart bomb damages one it is tinted toward white and swells up to 12 % for `kHitFlashSecs` (0.14 s), so the tougher ships (Scoptiliptic splitters 3 hits, bombers 2, barrier drones 3; Neon Arena shielders, bombers, splitters, hives and the wave-scaled hit points) visibly react to each hit that does not kill them. A Neon Arena shielder also flashes when its shield deflects a shot. `Droid::flash` / `NeoFoe::flash` hold the timer, `HitTint` does the colour mix, and the draw loops apply it (no extra draw calls).

**Super Stardust hit highlight.** Every enemy and asteroid that takes damage (flame, spread, beam, missile or missile splash) is highlighted for `kStarHitFlash` (0.14 s): its body tints toward hot white-yellow and swells up to 12 %, so each hit is visible even as enemies get tougher (+1 hit every 3 waves, capped at +8). The white Tie fighter is drawn slightly dimmed at rest so the flash shows on it. The timer is `StarFoe::flash`.

**Super Stardust laser look.** The Laser is an instant beam (`StarLaserFire` does the damage, the draw code in the planet scene does the look). It is drawn as four nested ribbons (outer halo, coloured glow, bright band, white-hot core) over 24 curved segments; its colour drifts from cyan at the ship to violet at the tip and slides with time, a bright pulse band streams outward and swells the core, the width shimmers and grows with the Laser's POWER level (up to +45% at 10), five energy beads flow along it, and flares sit at the muzzle and at the tip. Cost: about 100 quads plus a handful of small meshes per frame, all from stack data (no allocation).
- **Power-ups:** a green asteroid drops a gem when it breaks (the whole rock always; its pieces sometimes): **fire rate**, **power**, **shield** (12 s, destroys what it touches), **extra life** or **smart bomb**. If it is not picked up within **10 seconds it changes** to a type it has not been yet; once **five different types** have gone unclaimed it becomes a **bonus-points pickup** (500 x wave), which vanishes after another 10 s. Gems flicker before they change. Each is a small sphere (radius 0.44, bonus 0.52 - about two thirds of the earlier size) with a large white letter (R / P / S / L / B / $) on a dark outline that always faces the camera and swings gently left and right, so the type can be read at a glance (`StarPickupDraw`).

**Morpheous: the 12-stage ship and the swarm.** Every gold power pod is one build stage and each shows its hardware on the ship: 1 wing pods, 2 twin guns, 3 spread, **4 side guns** (a shot out of each wing at right angles), **5 rear gun**, **6 homing missiles** (a pair every 1.1 s that steer onto the nearest enemy), 7 five-way spread, **8 laser** (an instant beam through everything ahead), 9 triple rear fan, 10 doubled side guns and four missiles, 11 two option drones that fire too, 12 **full armada** (six missiles, a wider and faster laser, a golden halo). Blue pods give **shields, up to 5**, and green pods raise your **travel speed, up to level 5** (each level adds thrust: about twice the top speed at level 5; the HUD shows SHIELD n / 5 and SPEED n / 5). A carrier drops a gold, blue or green pod at random (half gold, a quarter each of the others). A hit strips three stages. **The higher the wave the further enemies detect you** (14 + 3 per wave, up to 90 units) and the harder they are to shake off; from wave 2 a ship that has spotted you **calls every ship near it** (the alert radius grows with the wave) and from wave 10 the alarm spreads across the whole arena, so everything **swarms** you at once.

**Explosions.** `MyExplosionFX` (in `Vectors.h/.cpp`, pure math like `MyCubeExplosion`) adds a family of effects on top of the cube debris: **Shockwave** - a thin ring that ripples outward from the blast, getting thinner and fading as it stretches (with an optional second, fainter ripple behind it); **Fireball** - a glowing sphere that swells and cools from white-hot through the blast colour to dark red; **Sparks** - fast streaks; **Smoke** puffs; **Implosion** - a ring that collapses into a point; **Flak** - a black puff with a red core; and the composites **Blast** and **Nova** (the big one: two shock-waves, a white-hot core, 26 sparks and smoke). `Boom()` chooses automatically by the size of the blast (small hits: sparks; medium: a ring and sparks; big: Blast; the player's ship, bosses and the Death Star: Nova), rings lie flat on the ground in the top-down games and face the camera in the others, and the smart bombs in Guardian, Neon Arena and Gyrospace add a big ring of their own. Particles are drawn with an opacity that fades as the effect ages.

**Planet Assault's worlds.** The planet is now a dense icosphere (2562 vertices, 5120 faces, plus finer 1280-face cloud and atmosphere shells) so flat shading reads as a smooth, detailed surface, with 20 brightness steps for a smooth day / night terminator. A **new world every 5 waves**: stage 0 is the real **Earth** (faces coloured from a 512 x 256 NASA Blue Marble map, `Assets/planets/earth_hi.rgb`, with relief on the land), then a **Mars**-like desert, an **ice world**, an alien **jungle world**, a **volcanic world** (seas of lava), a **golden world** and an **ocean world**, then round again. The invented worlds use continents from noise, ridged mountain relief and smooth colour blends (seas that deepen from a surf line, beaches, lowland, forest or desert by moisture, rock and snow by height, soft polar caps); changing stage only repaints the faces from a new palette and tints the clouds and atmosphere. The HUD names the current world.

**The fleet.** Fifteen hand-modelled spacecraft (`BuildFleet(g_fleet)` in `VectorShapes.cpp`, `g_fleet[]` in `main.cpp`, ids `kFlDart` ... `kFlStarhawk`) are used by the shoot-'em-ups: **Dart** (needle interceptor), **Manta** (flying-wing ray), **Lancer** (forward-swept gunship), **Scarab** (armoured beetle bomber with mandibles), **Wasp** (segmented stinger with glass wings), **Cruiser** (capital ship with a bridge, turrets and an engine bank), **Carrier** (hangar ship with a glowing pod bay - the ships that carry power-ups), **Dreadnought** (boss battleship), **Drone** (ringed sentry pod), **Falcon** (four-wing heavy fighter), **Phantom** (faceted stealth wing), **Saber** (twin-boom strike craft), **Hornet** (canard fighter), **Raven** (bat-wing raider) and the player's **Starhawk**. Hull panels take the instance colour so a game can tint them; glass, weapons, nozzles and the glowing engines are fixed. They are built from square-section bars, tapered slabs and thin double-sided wing panels. Gyrospace picks a different pair for every sub-wave (Cruisers for the boss squadron), Project X maps its enemy types and bosses to them (Carriers lead the groups that drop capsules, a Dreadnought is the boss), Morpheous, the spiral tunnel and Scramble's bandits use them too, and the Starhawk is the player's ship in Gyrospace, the tunnel and Project X.

**Gyrospace barrier drones (Geometry Wars style).** The second sub-wave of a warp (wave 2 on, 55 %, never together with the drone trio) can send a **barrier pair** (`GyBarrier`, `GyStartBarrier`, `GyBarrierPoint`): two electric-blue drones with an **animated electric arc** (three camera-facing ribbons, re-formed every frame) between them along the rim pick **one fixed direction - aimed at where the ship is when they appear (give or take 60 % of the half-span)** - and come **straight at it**: they rise out of the planet along fixed radial lines in 4.6-6 s (speeding up as they near), rush through the ship's ring in 1.3 s and fly on past the camera. They **never turn, sweep, spiral or hunt** (`GyBarrier::phi / spread` are constant). While both live and the pair is passing through the ring, **touching the arc kills the ship** (the ship is caught when its angle lies between the two drones; the arc is dimmer while the pair is still climbing): move out of the span in time. **Shoot one drone and the arc is broken** - it is harmless and not drawn - and the survivor simply **carries on along the same straight line** (it does not hunt); ramming a drone still kills the ship. Hit points 1 + wave / 5 (max 4); 150 points each, +100 if both go; the smart bomb removes both. A **humming electric sound** (Decention's `dc-arc.wav`, `kSfxDcArc`, written by `DcMakeArcSound` the first time if missing) loops while the arc exists: it starts **quietly** when the pair appears at the planet, **swells as it climbs and closes in on the ring**, grows further as the ship's angle nears the arc, and stops when the arc breaks or the pair leaves. `BotGyrospace` shoots the barrier drones first and slips out of the arc's nearer end.

**Planet growth between warps.** The Gyrospace planet no longer pops to its new size when a new warp starts: the drawn base radius (`GyGame::planetBase`) eases toward `6 + 2.6 x warp` (exponential approach, about 0.5 s to settle, `g_hudDt`-based so it is frame-rate independent). A new planet (`stage` changes) and a new game start at their first size at once.

**Gyrospace planets.** Each stage shows the real planet - Pluto, Neptune, Uranus, Saturn (with rings), Jupiter, Mars, Earth - a **dense sphere** (20480 faces at full detail, 5120 when small on screen or at a low Vector Quality level; `PlanetLod`) whose faces take their colours from baked NASA / Solar System Scope surface maps in `Assets/planets/` (credits in `PLANET-MAPS-CREDITS.txt`). The maps are sampled **bilinearly** (no blocky texels). Gas giants get **turbulence-warped bands** with streaky detail and an **oblate** shape (Jupiter 0.935, Saturn 0.90); Pluto and Mars get fractal relief and **impact craters** built into the geometry (so they light and shade properly) with darker crater floors; Earth uses the sharper 512 x 256 map with raised mountain land and a drifting **cloud layer**. Every planet has a faint **atmosphere shell** tinted for it (`kGyHaloCol`). Saturn's rings are a real system: C ring, five-band B ring, the empty Cassini division, A ring with the Encke gap (96 segments x 10 bands), with per-segment grain, face lighting and the **planet's shadow** cast across them. `BuildMappedPlanetMesh(mesh, mapFile, subdiv, kind)` (kind 0 Pluto, 1 Neptune, 2 Uranus, 3 Saturn, 4 Jupiter, 5 Mars, 6 Earth) accepts 128x64, 256x128 or 512x256 maps. **Super Stardust's** planets have the same two detail levels (subdiv 4 / 5, `g_planet` / `g_planetHi`, `g_planetEarth` / `g_planetEarthHi`), 6-octave terrain, deeper relief and domain-warped swirling clouds; in play the dense tier is drawn as **quads** (`g_planetHiQ` / `g_planetEarthHiQ`: neighbouring subdiv-5 triangles merged by `BuildPairedMesh`, repainted by `RefreshPairedColours` on a stage change) - about 10,240 faces, half the D2D figures; the engine limits `kMaxPolys` (32768) and `kMaxMeshVerts` (10300) were raised for them.

**Chomper and Chomper 3D - maze, tunnels, ghosts and light.** Mazes are carved with a strong straight-line bias, so lanes run long, and no wall is ever a single block (a lone pillar is removed; every wall piece is at least two tiles long). One row (two on tall mazes) is a **wrap-around tunnel** as in the arcade: its left and right edges are open and joined to the maze by straight five-tile lanes, and leaving one side brings Chomper and the ghosts out on the other (`PacWx`, `AdvanceActor`; the mouths carry no pellets). **The ghost house.** A real cage is built in the middle of every maze: a 7 x 5 tile block of wall with a 5 x 3 hollow interior (purple floor), a pink **gate** bar in the middle of its top wall and a lane running all round it that joins the rest of the maze (the carve goes round the house; `PacBuildMaze` step 3c, drawn by `PacDrawHouse`). The four ghosts wait side by side inside; when their release timer ends they walk out through the gate, and eaten ghosts come back in through it. Chomper cannot enter the cage and ghosts outside it never wander back in (`PacInHouse`). **Eaten ghosts** (eyes) now steer home along a breadth-first distance field (`PacBuildHomeDist`) and are revived the instant they reach the ghost house - the old test only passed on an exact frame, so they never came back. They then stay caged for **60 s on level 1, 6 s less each level (never under 6 s)** before the chase resumes. Chomper's light uses the same fine smoothstep fall-off as the Gauntlet torch (14 floor rings, walls near him built from four separately lit sub-blocks) but the far end is a dim blue, never black, so the whole maze stays visible.

**Chomper 3D camera.** The camera is now much higher and further out. **B / button 2 turns it 45 degrees per press**; the stick is relative to the camera (up = the way the camera looks), so the maze stays easy to read from any angle.

**Start-up.** The program opens with the CPGE intro (the engine's splash movie, about 10 seconds), fades to black, and then the vector demo begins. If the intro movie file is missing the demo starts immediately.

**Dungeon monsters** are hand-modelled and articulated (the "creature kit" - `AddBar`, `AddTaper`, `BuildCreatures` - in `VectorShapes.h/.cpp`; `SubmitCreature` in `main.cpp` draws them): a body mesh plus separately swung arms, legs and wings, built from bars, tapers and 8-sided cones. The **grunt** is a green brute with a horned iron helmet, tusks, spiked pauldrons, a loincloth and a spiked club; the **ghost** hovers in a ragged sheet with hollow eyes, a wailing mouth and clawed arms reaching forward; the **demon** has horns, fangs, burning eyes, a spaded tail, flapping bat wings (bones plus a two-sided membrane) and a pitchfork; **DEATH** is a tall hooded figure with a black void for a face, red eyes, bony hands and a scythe.

**Game over.** The GAME OVER text is drawn as animated vector strokes in the game's own colour: the letters drop in one after another, then bob, swing about their vertical axis, pulse and flicker, each stroke drawn as a wide glow, a body and a white-hot core (`DrawVectorBanner`).

**Clean screen during games.** In the arcade games only the game's own text is drawn: the score, lives, wave, status line (weapon, build, shield...), banners, the hall-of-fame table and radar. The scene title, the control hints, the renderer / FPS line, the key list and the "now playing" line are hidden; **Alt+H** brings all of them back (and hides them again). Pausing shows just "PAUSED".

**Hotkeys that work in every scene.** **Alt+D** toggles the debug display (this replaces the engine's F2 toggle, which was removed; the setting is saved to the config); **Alt+O** opens the engine's options window, and the game is **paused while it is open** and resumes when it is closed (Esc or its Close button). Alt+H lists these keys on screen.

**Project X (Ctrl+F1) - land, formations, starfield and bosses.**
- **Surfaces fade in and out.** At every new wave `PxPickSurfaces` picks at random open space, a floor, or (level 3+, never on a boss wave) a floor and a roof. A surface that changes fades over about 3.5 s: it rises out of the bottom / lowers from the top (`g.floorFade`, `g.roofFade`, eased by `PxEase`; a surface that is not up sits 3 units off screen, so every height test just works). Ground features (below) appear only while the floor is more than 60 % up and sink away with it.
- **The floor and roof are deadly.** Touching either destroys the ship at once (`PxDie`): no shield saves it and respawn invulnerability does not apply.
- **Smooth land.** The land is built from world-fixed columns (`kPxColW` = 0.25 wide, each with one height and a lighter cap slab) that scroll rigidly; before, columns sat at fixed screen positions and re-sampled the height each frame, so the steps rippled. Tops are toned by slope.
- **Formations never cross the land.** Ships riding in from the sides are held in the passage between floor and roof by a *soft* clamp (`PxSMax` / `PxSMin`: no kink in their path); top entries SWOOP (dive to just above the floor, turn in a rounded bottom, climb back out); entries from below are gone (a would-be bottom group is now a **ground assault**: a cluster of ground features instead of ships). Fixed bugs: left / right entries used to be pinned to the screen edge by an x clamp, a sweep path started and stopped dead, and hard clamps made kinks; ship heading is now rate-limited to 200 degrees a second on top of its smoothing.
- **Ground features.** Turret, heat-seeking rocket launcher (level 3), **flak cannon** (level 4: a fan of three bolts) and **flame vent** (level 2: indestructible; 3.6 s cycle of rest, 0.8 s warning glow, 0.9 s eruption of a 3.2-high column that burns a ship in it).
- **Faster as you progress.** Formation speed, group release rate and enemy bolt speed scale with the wave and now +10 % per level (up to +90 %), `fm` in `GameProjectX`.
- **Three-layer starfield** (`PxBack`, the 2D pre-pass `g_hudPre`): 70 / 46 / 26 stars drifting at 8 / 22 / 50 % of the land's speed, twinkling, behind everything.
- **Fifteen bosses** (`kPxBoss`, one per level, then round again; health = hp0 + hpLv x level): DREAD, CRUISER, WARDEN, AEGIS, TEMPEST, SPECTRE, MARAUDER, ORACLE, PHOENIX, LEVIATHAN, COLOSSUS, OVERLORD, MONSOON, STARBURST, MATRIX. Each has its own ship, size, hit radius, one of five smooth movement patterns (sine, circle, sweep, charge, figure of eight; `PxBossPos`) and a cycle of up to three attacks from the shared `AtkPattern` library fired in the playfield plane (`PxBossFire`). The HUD shows the boss's name and health bar. Enemy bolts: up to 120 at once (`kPxFoeShots`).

**Space Spartans missiles.** Every missile fired at the player (missile, micro-missile swarm, heat-seeker and torpedo) is drawn as Scramble's red missile with its animated tail flame (`DrawScrambleMissile`: the rocket model at uniform scale plus the thin white flame mesh pulsing at 40 Hz); the torpedo is the same model, larger and more orange.

**End key.** During real game play (not in a game's demo) **End** ends the current game at once: the game freezes, GAME OVER shows, `MyHighScores::Update` decides whether the score makes the table (if it does, the name-entry screen follows), then the hall of fame and the demo resume exactly as after a normal game over (`g_endGame`, read by `DrawGameHud` and `Overlay`). It does nothing in demo mode or the vector demonstration scenes.

**PAUSED banner.** The six letters spin about the horizontal axis at one common speed and start flat; letter i begins 0.12 s after letter i-1, so a clean wave of rotation runs along the word.

**Settings and game play.** Alt+O is ignored during real game play (a game scene whose demo / attract mode is off, including GAME OVER and high-score name entry); it works in the vector demonstration scenes and while a game's demo is running. If any setting is changed while a game's demo is running, the demo is **restarted as a fresh session** (fade in from black, game state reinitialised) as soon as the options window closes, because changed quality / effects / audio values left some games' demo state corrupted. The engine's apply callback calls `VectorDemo::NotifySettingsChanged()`, which sets an atomic flag the frame loop consumes once the window is closed.

**High scores.** Every game keeps a **top-5 table** (score and wave for each entry) plus the highest wave ever reached. The best score and top wave are shown top-left (gold with "NEW HIGH SCORE!" while you beat it) and the top 5 is shown only by the vector hall of fame (`DrawHallOfFame`; the old plain-text table has been removed). When a game ends **without** a top-5 score the GAME OVER banner stays for 2.5 s, then the hall of fame drops in, is held 8.5 s, scrolls out and the demo resumes (`StartHallAfterGame`). Each entry has a **name**: when a finished run makes the top 5 the game pauses and asks for it on the vector name-entry screen - type letters/digits and press Enter, or on a pad scroll with up/down, Fire adds the character, Bomb deletes, Start confirms - and then the hall of fame shows the new line highlighted. Adding a letter plays `sx-select.wav` and going back one letter plays `sx-click.wav` (`kSfxNameAdd` / `kSfxNameBack`, only when the name really changes; re-point either at another effect with a one-word change). A run you abandon (restart or switch game) is entered under the last name used. The tracking is the `MyHighScores` class in `Vectors.h/.cpp`:

```cpp
MyHighScores scores;
scores.SetFile(assetsDir / L"highscores.txt");   // loads the saved table once
scores.SetActiveGame(sceneIndex);                // when a game starts (flushes the previous one)
scores.ReportWave(wave);                         // whenever a new wave begins
scores.Update(score, gameOver, dt);              // once per frame
if (scores.NeedsName()) scores.SubmitName(typedName);   // a top-5 run has ended: pass in the player's name
int best = scores.ActiveBest().score, topWave = scores.ActiveBest().wave;
for (int rank = 0; rank < MyHighScores::kTopCount; ++rank)      // the top-5 table of the active game
    printf("%d. %06d  wave %d
", rank + 1, scores.Top(scores.ActiveGame(), rank).score, scores.Top(scores.ActiveGame(), rank).wave);
```

The table is saved to `Assets/highscores.txt` (`S game score wave name` per top-5 entry and `W game bestWave`) on game over, when you switch game, and every 5 seconds if something changed - never every frame.

**Enemy ships.** Enemies are modelled craft rather than abstract shapes: the **raider** (forward-swept wings, glowing red cockpit eye, twin engines), the heavy **warship** (armoured block hull, bridge tower, side pods, three engines), the **TIE fighter**, the **flying saucer** (domed, rim lights, landing legs), the spiky **mutant**, and voxel creatures. Raiders and warships turn to face where they fly. Scramble's ground enemies are a banded **fuel tank** and a rotating **anti-air turret**, its rocket has fins and a flame, and its **bombs** are finned bombs that fall nose-down spinning only about the vertical axis. The player ships in Asteroids, Scoptiliptic and Scramble were made smaller.

**Performance.** The games are CPU-light; the cost is in drawing polygons, so the rendering core was optimised:

| Optimisation | Effect |
|---|---|
| Polygon points live in one compact pool (`g_pts`) instead of a 272-byte zero-filled struct per polygon | far less memory traffic per frame |
| Instance culling: objects behind the eye or off screen (bounding sphere) are skipped before any vertex maths | |
| Level of detail: an object that projects smaller than ~6 px becomes one hexagon, smaller than ~1 px vanishes; shadows under tiny objects are skipped | |
| Polygons that are off screen or smaller than a pixel are dropped | |
| Shading is quantised to 11 steps so neighbouring faces share a colour | bigger batches |
| **Batched fills**: consecutive polygons of one colour become ONE path geometry and ONE fill call (`Renderer::DrawFilledPolygonBatch`, DX11 / DX12 / Vulkan; figures are given a common winding so overlaps union instead of cancelling) | shadows, grid lines and pellets collapse into a handful of calls |
| A planet's visible faces never overlap, so `SubmitMeshGrouped` gives them one depth plus a tiny per-colour rank: faces of one colour end up adjacent and merge, so ~160 terrain faces become a few dozen fills (the whole atmosphere shell is a single fill) | |
| Preallocated everything: meshes and voxel sprites are built once; polygon, key and batch arrays are reserved up front; explosions use a fixed pool | no allocation in the render loop |
| Off-screen parts of big worlds are never submitted (Chomper 3D submits only the tiles around Chomper; the planet game skips enemies on the far side) | |

The footer shows `N polys in M draws (L LOD, C culled)` so the effect of all this is visible on screen.

**Difficulty ramps up gradually.** Clearing a wave announces `WAVE n` and makes the next one a little harder (every ramp is capped so the games stay playable):

| Game | What grows with each wave |
|---|---|
| Asteroids | One more rock per wave (up to 12); rocks 10% faster per wave (up to 2.2x). |
| Guardian | More landers (5, 7, 8, 10 ...); landers 8% faster, mutants 6% faster; from wave 3 some landers start as aggressive mutants. A new wave's ships do **not** appear in place: each waits a random 0 - 2.5 s above the top of the screen and then glides down slowly (`kDefEntrySpeed`, 1.5 units a second) to its position, harmless meanwhile (it does not hunt, abduct, fire or lay mines until it arrives - `DefEnter`). |
| Scoptiliptic | One more droid per wave; droids and the chaser faster; mines laid sooner; the chaser arrives earlier and turns harder. |
| Scramble | A wave is 450 m. The scroll speeds up, saucer formations become more common, missiles launch from further away, fuel burns faster, spawns are denser, and each wave gives a bonus. |
| Chomper | Ghosts faster and released sooner, power pellets last shorter. |
| Planet Assault | More enemies per wave, faster; spirals and bombers join from waves 2 and 3; an extra smart bomb per wave. |
| Neon Arena | A bigger quota, faster spawns and faster enemies; one more enemy type in the random line-up every two waves. |
| Trench Run | More TIE fighters, faster and more accurate fire, denser turrets and beams, a faster trench. In the SPACE BATTLE the TIEs now spawn spread across the whole screen (`TieFoe::ox / oy`, up to +-58 x +-28 units at z 95) and converge on their weave spot in the ship's box by z 45, instead of all bunching in the middle; the 2D star layer covers the full screen height (70 stars). |
| Voxel Guard | More abductors, then bombers (wave 2), swarmers (3) and guardians (4), then the newer types below (waves 4-11); everything faster; every fifth wave is a boss. See "Voxel Guard (Resogun): enemies, bosses, spire and floor". |
| Chomper 3D | As scene 10. |

`WaveScale(wave, perWave, cap)` in `main.cpp` is the one-line helper behind all of them.

**Models.** Everything is built from convex polygon meshes with per-face paint (`tone`, fixed colour, glow), so one mesh can have glass, metal, team-coloured hull and self-lit engine glow. The player ship (a fighter with tapered fuselage, canopy, swept wings, tail fins and twin engines with animated flames), the rocket, the saucer, the mutant, the humanoid and the ghost are all built by small builder functions (`BuildShipMesh`, `BuildRocketMesh`, `BuildSaucerMesh`, `BuildMutantMesh`, `BuildHumanMesh`, `BuildGhostMesh`) that compute their own face normals. Characters keep their natural orientation (ships bank, saucers spin and rock, ghosts face their direction of travel); the spiky mutants, rocks, shots and explosion cubes tumble on all three axes.

**Background music.** When the demo starts it picks a **random `.mp3` from the `Assets/` folder** (subfolders included) and plays it through MCI, honouring `playMusic` and `musicVolume` in `GameConfig.cfg`. When a song ends another random one starts, and the footer shows "Now playing". If the folder has no `.mp3`, a random tracker `.mod` is played by the engine's `MODPlayer` instead. Just drop more `.mp3` files into `Assets/` and they join the shuffle.

**Common game features**

* **Every flying object tumbles a full 360 degrees on X, Y and Z.** Each object owns a `Tumble` (three angles advancing at three different speeds of at least 50 degrees per second), turned into a `MyQuaternion::Euler` each frame.
* **Shadows.** Every object casts a planar projected shadow: its vertices are slid along the light ray onto the floor (Asteroids, Scoptiliptic, Chomper) or onto the terrain surface under the object (Guardian, Scramble). Only faces turned toward the light are projected, whose union is the exact silhouette.
* **Vector scoring.** The score is drawn with stroke-built seven-segment digits (each stroke is a filled quad). Every digit swings about its own vertical axis with a small perspective, and the digits "pop" when points are scored. Lives are drawn as vector ship icons.
* **Cube explosions** on every kill, from the shared `MyCubeExplosion` pool.
* **Always on screen.** The playfield is computed every frame by unprojecting the screen corners onto the floor (or onto the side-view plane), so the game always fits the window, whatever its size.

### Dominion (F12) - the real-time strategy game, and the other recent changes

**Dominion** (scene 22, `GameRts`) is a Dune / Escalation style RTS. Project X moved to scene 23 (press **Ctrl+F1**, or Page Down from F12); its saved scores now live under that number.

* **Bigger every level.** The map is 128 x 128 tiles on level 1 and 16 tiles bigger each level, up to 256 x 256 (`g_rtN`; the arrays are sized for the maximum). The number of mining spots and bridges grows with the area, the radar keeps at most 64 x 64 cells, and the enemy starts correspondingly further away.
* **Workers and clearing land.** Trees block buildings, so the **Capital Fortress trains Workers** (you start with two). Select workers and either press **K** / the CLEAR button and click an area (every tree within about 2.4 tiles is marked with a red gem), or just right-click a tree. Each worker takes the nearest unclaimed marked tree, walks beside it, chops for about a second and the tree is gone: paths open up, the ground gets its grass colour back (only the neighbouring tiles are re-baked) and the spot can now take a building. Unreachable trees are given up with a message.
* **Map.** A random landscape every level: water, hills, mountains, forests, short **bridges** over narrow water, and gold / copper / silver **mining spots**. Both bases start on open land (a clear 11 x 11 patch), the enemy at least about half a map away; the generator guarantees a walkable route between them (it carves a bridge / pass if needed).
* **Economy and building.** You start with a **Capital Fortress** (it is required for everything). Buildings: **Extractor** (placed on a mining spot, pays 2.4 of that resource a second), **Barracks**, **War Factory** (needs Barracks), **Airfield** (needs Factory), **Gun Turret** (needs Barracks) and **Repair Pad** (heals units standing on it). Each costs gold / copper / silver, takes a base time to complete and shows a progress bar while it rises. Pick one from the bottom-right buttons, move the mouse (a green / red ghost shows whether the spot is valid - flat, explored land within 16 tiles of your buildings) and left-click to place; right click cancels, Shift keeps placing. Select a Barracks / Factory / Airfield and use the second button row to train units: **Rifleman**, **Medic** (heals nearby infantry and vehicles), **Scout** buggy, **Tank**, **Artillery** (long range, splash), **Fighter** and **Gunship** (aircraft). Units cost resources too and queue (up to 4 per building).
* **Controls.** Left click selects, left drag box-selects (Shift adds); right click moves, or attacks the enemy under the cursor; the mouse wheel zooms; WASD / arrows / the screen edge scroll; **Home** or the HOME button jumps to your fortress; drag on the radar to scroll, right-click on it to send the selection there. **T** (or ATTACK) then click = attack-move, **K** (or CLEAR) then click = workers clear trees, **V** = guard, **X** = stop, **G** = form a group, **Ctrl+1-9** = assign the selection to that group, **1-9** = select a group (twice quickly = look at it). **Vehicles and aircraft must be in a group before they accept orders** (infantry can be ordered freely); a group advances together at the speed of its slowest member.
* **Fog of war.** Unexplored land is black; explored land is dimmed; only what your units and buildings currently see is bright. The radar is fogged the same way and enemy units never appear in fog (they cannot be clicked either). The white box on the radar is exactly the part of the map the camera shows.
* **Health bars.** Selected units and buildings show a bar above them: **green above 25 %, red below**. Buildings under construction always show a yellow progress bar; a selected production building shows its training progress.
* **The AI.** One opponent builds in a fixed priority order (extractors, barracks, factory, repair pad, turrets, airfield, then more), trains a mix of units, sends a scout to spy on your base now and then, defends when you come near, and launches ground + air attack waves at your fortress once its army is big enough. Ground units follow a **flow field** (breadth-first distance map) built for the target tile, so they walk real paths around water, mountains, trees and buildings; one field is shared by every unit going to the same place (cached, built at most three per frame). **Each level** the AI builds and trains faster (`RtAiBuildMul`), moves faster (`RtAiSpeed`), earns more (`RtAiEco`), starts with more units and resources, and its units and buildings are a little tougher and hit harder. Destroy its Capital Fortress to win the level (+500 x level, a fresh map); lose yours and the game is over (high score table as in the other games).
* **Textured terrain.** Six CC0 diffuse maps from Poly Haven (grass, hill grass-and-rock, mountain rock, shore sand, bridge planks, forest floor) are stored as 128 x 128 raw RGB in `Assets/textures/` (see `TEXTURE-CREDITS.txt`). `RtBakeTiles` samples them once per level into four sub-quad colours per tile (about 21 tiles per texture repeat, so the ground shows real grain), adds a sand fringe where grass meets water, forest floor under trees and deeper blue away from the shore. The camera starts further out (distance 42) and the wheel now zooms between 20 and 72. Close up (under about 800 visible tiles) every tile is drawn as four textured quads; at the default distance one quad per tile; zoomed far out 2 x 2 or 3 x 3 tile blocks. If a texture file is missing the old flat colours are used. Mountains, hills and bridge planks are tinted from the same baked texture colours.
* **Cost control.** Terrain is one quad per tile (or 2 x 2 / 3 x 3 blocks when zoomed far out; the polygon limit was raised from 6144 to 8192), mountains / trees / hills have per-frame budgets, the radar is cached as merged runs, fog refreshes four times a second, and pathing is one BFS per goal rather than per unit.
* **Mouse support added to the demo.** `RtPollMouse` reads the cursor (scaled to the render size) and the buttons; `WindowProc` hands the wheel delta to `g_wheelAccum` while the RTS is active (so the engine camera does not zoom). The digit keys are group keys in this scene instead of scene switches.

**Also changed.** *Guardian*: the camera is zoomed further out, the landscape is much lower, and the radar sits above the score; the radar strip spans the whole wrapped world and a white box shows the part the camera really sees (it follows any zoom change). *Resogun (Voxel Guard)*: a wide, deep twisted **spire** on a glowing tech-floor base stands on the axis of the cylinder - the centre of the circular playfield, in the middle of the green mass - and turns as the ship moves; enemy bombs are bright flashing magenta with a halo, your smart bomb has a visible blast, and its radar is above the score too (with a camera box). *Gauntlet*: the hero carries a torch - the maze is lit near the hero and falls into darkness with distance (a pool of light on the floor, per-tile wall brightness, monsters and items fade and are not drawn beyond the light). The fall-off is a smoothstep to near-black; the floor glow is 22 fine rings and every wall tile inside the light is built from four half-size blocks, each lit by its own distance, so the light fades smoothly across walls. Monsters darken completely with distance: `g_meshLight` is a global brightness multiplier in `SubmitMesh` (creature faces have fixed paint that ignores the instance colour, so tinting the colour did nothing), set by `SubmitCreature`'s `light` argument. *Super Stardust*: asteroids bounce off each other and break up (the smaller chunks only bounce); ships that hit an asteroid are destroyed so they steer round rocks; orbiters, spiralers and bombers now shoot at you, faster and with more bolts as the waves climb. *Morpheous*: ships dodge asteroids and asteroids bounce off each other. *Chomper / Chomper 3D*: ghosts take random turns at junctions on early levels (65 % on level 1, dropping 8 % a level), and Chomper lights up the area around him (walls, pellets and ghosts darken with distance; the floor has a pool of light). *Marble Madness*: the marbles are far more detailed (320 faces, swirled), the red spike roller is a real spiked ball, the jump is much higher (it clears rollers, hoppers and other marbles) and a roller can be squashed by landing on it from above. The high-score *name entry* text is now centred and split so it cannot run off the screen.

**Sound effects from the Sonniss GDC bundle.** The folder `G:\Studio\GameSFX` (10,000+ royalty-free WAVs, no attribution needed) was surveyed; its `A_Known_Games/Stellarius` pack (space-game weapons, explosions and strategy-game interface sounds) was the best fit. 34 clips were converted with ffmpeg (mono, 16-bit, 44.1 kHz, loudness-normalised, trimmed, about 3.5 MB in all) into `Assets/sx-*.wav` (see `Assets/SONNISS-GDC-BUNDLE-NOTE.txt`) and added to the `SfxId` enum / table as ids 36-69. Where they are used: **every explosion** (`Boom` with the default sound) now picks a sample by size - small blasts keep the old sample, medium ones alternate `sx-boom1/2`, big enemies use `sx-shipboom`, bosses and large blasts `sx-shipbig`; **enemy shots** in all shooters are now `sx-plasma` bolts; **smart bombs** (every game) play `sx-bigboom`; Super Stardust's weapons each sound different (flamer = `sx-flak` whoosh, `sx-laser` spread, `sx-rail` piercing laser, `sx-torp` missiles) and its shield clangs (`sx-shield`); Morpheous missiles are `sx-torp` and its laser beam `sx-rail`. In **Dominion**: riflemen fire `sx-cannon`, scouts `sx-point`, tanks `sx-mdriver`, artillery `sx-arty` + `sx-artyhit`, fighters `sx-laser`, gunships `sx-missile`, turrets `sx-flak`; unit deaths `sx-smallboom` / `sx-shipboom`; buildings collapse with `sx-stationboom` + `sx-destroy`; interface sounds for clicks, selecting (`sx-armysel` / `sx-select`), move orders, placing a building (`sx-place`), construction complete (`sx-built`), a unit trained (`sx-trained`), queuing (`sx-queue`), refused actions (`sx-invalid`), "base under attack" (`sx-attack`, at most every 8 s) and the enemy wave warning (`sx-incoming`, the voice "Incoming fire"). Unused but converted: `sx-repair`, `sx-war`.

**Guardian's full cast.** Besides the landers (and the mutants they become) the classic enemies are in: **bombers** (from wave 2; cruise across laying **mines** that hang for 14 s - shoot them or fly round them, 250 points), **pods** (from wave 3; drift about, 1000 points, and burst into five **swarmers** when shot - a smart bomb does not release them), **swarmers** (fast, weaving, they shoot) and **baiters** (after 40 s in a wave a fast hunter that shoots a lot arrives every 10 s, up to three; they never hold the wave open). Mutants, swarmers and baiters now shoot at you, faster as the waves climb; a smart bomb clears mines and enemy shots on screen. **Frenzy:** when the last human dies the planet is lost - a big explosion, the ground turns red, every landers becomes a mutant, and ALL ships (bombers, pods, swarmers too) hunt you flat out at 1.7x speed and shoot twice as often until the wave is cleared; the next wave restores humanity and the calm. The radar colours: landers red, mutants violet, bombers orange, pods yellow, swarmers pink, baiters cyan.

**Torch-light and rocks:** in Gauntlet the health orbs, gold chests, spawn stations and the stairs all fade into the dark with distance like the walls and monsters. In Asteroids (and Stardust) rocks bounce off one another without breaking; only your shots split them, and a broken rock bursts as a yellow fireball with a red-orange core (not rock brown).

### Writing your own game on this framework

```cpp
// 1. Per-frame, inside your scene function:
renderer->myCamera.SetOrbitView(0.0f, 0.0f, 0.0f, 66.0f, 0.0f, 17.0f);   // the ENGINE camera: target x, y, z, pitch (looking down), yaw, distance
renderer->myCamera.GetViewFrame(g_f.camPos, g_f.viewRot, g_f.worldFromView);   // hands the engine camera's view to the software renderer's frame
const Rect2 field = FloorField(0.0f);                    // the visible floor rectangle (x0..x1 along X, y0..y1 along Z)
g_hud = MyHud;                                           // 2D HUD pass, drawn after the 3D faces

// 2. Read input:  g_in.mx, g_in.my (-1..1), g_in.fire / fireEdge, g_in.alt / altEdge, g_in.startEdge

// 3. Queue geometry:
SubmitFloor(field, floorColor, gridColor, 2.0f);
SubmitObject(g_octa, tumble.Q(), MyVector(1, 1, 1), pos, color, 0.0f, shadowColor, kDepthShadow);   // lit mesh + shadow
Boom(pos, 24, 6.0f, 0.2f, color);                        // explosion
SubmitBooms(0.0f);                                       // draw the debris (floor shadows at y = 0)
```



### How the renderer hook works

```cpp
// Renderer.h
using Overlay2DCallback = void (*)(Renderer& r, int clientWidth, int clientHeight);
inline Overlay2DCallback g_Overlay2DCallback = nullptr;

// Your code, once:
g_Overlay2DCallback = MyOverlay;
```

DirectX 11, DirectX 12, OpenGL and Vulkan each call the callback once per frame from inside their 2D pass, after the scene's own 2D content and before the FPS text. Draw with `DrawRectangle`, `DrawFilledPolygon`, `DrawLine` and `DrawMyText`. The callback runs on the render thread: do not block in it.

`Renderer::DrawFilledPolygon(points, count, color)` fills a **convex** polygon. DX11, DX12 and Vulkan override it with Direct2D path geometry (antialiasing is switched off during the fill so adjoining faces have no seams). OpenGL overrides it with a triangle fan on its 2D shader. The base class has a scanline fallback built on `DrawRectangle`.

### Performance notes

* No heap allocation after the first frame: face and key arrays are reserved once and cleared each frame.
* One combined 3x3 matrix plus translation per object; back faces are culled before projection.
* One `FastSinCosDeg` per rotation; `RotateAroundBatch` amortises it over the whole array.
* Each `FAST_*` lookup increments an atomic statistics counter, so hoist lookups out of inner loops.
* Direct2D creates a path geometry per polygon (the same cost model as the engine's `DrawCircle`/`DrawCurve`). A few hundred faces per frame is comfortable; thousands would call for a native triangle batch. The Chomper maze is the busiest scene (walls are culled when buried in solid rock to keep it down).

## Testing snippets

```cpp
// Run once after FAST_MATH.Initialize() to sanity-check the build.
bool VectorSelfTest()
{
    auto approx = [](float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; };
    bool ok = true;
    ok &= approx(MyVector::Angle(MyVector::Right(), MyVector::Up()), 90.0f, 0.05f);
    ok &= approx(MyVector::Angle(MyVector::Right(), MyVector::Right()), 0.0f, 0.05f);   // accurate near parallel
    ok &= approx(MyVector::Angle(MyVector::Right(), MyVector::Left()), 180.0f, 0.05f);
    ok &= (MyVector::Cross(MyVector::Right(), MyVector::Up()) == MyVector::Forward());
    ok &= (MyVector::Reflect(MyVector(1, -1, 0), MyVector::Up()) == MyVector(1, 1, 0));
    ok &= approx((MyVector(0, 0, 1).RotatedY(90.0f) - MyVector(1, 0, 0)).Magnitude(), 0.0f);   // tables are ~1e-4 accurate
    ok &= approx((MyQuaternion::Euler(10, 20, 30) * MyVector::Forward()).Magnitude(), 1.0f);
    return ok;
}
```

The sin/cos tables resolve about 1e-4 radians, so compare rotated vectors with a tolerance rather than exact equality.

## Known limits

* `MyVector::operator==` is approximate (Unity semantics); use `Equals` for bit-exact comparison.
* `Reflect` and `ProjectVectorOnPlane` assume a **unit-length** normal.
* `DrawFilledPolygon` handles convex polygons only (up to 32 vertices in the D2D paths).
* Non-uniform scaling of arrows only approximates face normals for lighting (the demo scales arrows along their length only).

## Recent gameplay / rendering changes (demo)

* **Level-name banner** (Scoptiliptic / Neon Arena field names): queued by the HUD (`QueueLevelBanner`) and drawn last by `Overlay()` on a dark plate, so nothing paints over it.
* **Terrain shadows** (Guardian, Scramble): `g_tshadow` (`TerrainShadowCtx`) makes `SubmitShadow` drop each vertex straight down onto the top of the column under it, centred in the column depth. Voxel Guard (`VoxShadow`) and Project X (`PxShadow`) use a cheap oval (`SubmitTopEllipse`). Project X's camera now tilts 8 degrees so the column tops are visible.
* **Scramble**: scroll starts at 2.6 and eases to 4.6 over the first 700 m (then the per-wave scale applies). Flying enemies clear the drawn column tops (`ScGroundTop`, `ScFlyFloor`); ground targets rest on the highest column under them.
* **Voxel Guard (Resogun)**: each wave set shows one of eight animated far-distance backdrops (ringed planet, mountains and aurora, sun, city skyline, thunderstorm, galaxy, space battle, volcano), tinted to match the floor colour. Boss rounds hide the humans (and spawn no abductors); they return, as they were, on the next wave.
* **Chomper / Chomper 3D Glitchy** (fifth ghost, purple; appears from wave 5, not drawn or active before): immune to power pellets (it never turns blue and can never be eaten, so touching it always costs a life). Slightly faster than the others (4.15 tiles/s on level 1, +0.30 a level, capped at 6.4 against the others' 5.6), still slower than Chomper early on. It pulses purple and cycles through disintegrating into shards and re-forming, with an occasional sideways glitch, but stays deadly the whole time.
* **Chomper / Chomper 3D transport spots** (level 4 and up): 2 fiery floor spots on levels 4-5, then 3, 4, 5, and 6 from level 12. Step on one and Chomper dissolves into a spinning beam, then re-forms at a random other spot (about 0.6 s each way, untouchable meanwhile; the spots recharge for 1.2 s). The spots pulse faster as he nears them, and ghosts ignore them.
* **Chomper / Chomper 3D vertical tunnels**: besides the left/right tunnels, one column (two on wide mazes) beside the ghost house is open at the top and bottom; leave through the top and you (or a ghost) arrive at the bottom, and vice versa.
* **Chomper / Chomper 3D lit walls**: walls inside Chomper's light animate (brightness ripples along the maze; the breathing / bobbing / hovering styles (bubbles, diamond pillars, spears, domes) also move, but the turning styles - boulders, crystals, totems, shards, step pyramids - only shimmer in brightness, via `mact` in `PacSubmitWall`). The lit-wall detail overlay (studs, climbing band, beacon gem; `PacWallDetail`) is switched off. Walls in the dark stay still.
* **Planet Assault flamer**: a slimmer (0.6 scale), longer jet (about 37 degrees of reach, narrower cone); each flamer power level adds +6% reach, +15% damage and an extra pierce every third level.
* **Planet Assault (Stardust)**: **B** toggles the camera between the close view and a zoomed-out view (eased, reset on game start; the attract-mode demo randomly starts zoomed in or out). The smart bomb moved to **Y** (pad button 4) / X / Shift.
* **Scoptiliptic / Neon Arena camera** (`ArCamera`) now always tracks the ship (no edge clamping).
* **Neon Arena gravity wells**: a dark whirlpool whose bands flow inward to a solid black hole (`NeoDrawWell`); anything (enemies, mines, the ship) reaching the hole (`kNeoWellHole`) is destroyed.
* **Spiral Tunnel**: 30-panel rings lit by the ship's lamp (smoothstep fall-off, near black in the distance, halo discs round the ship), smooth two-arm colour spiral; mutators PINCH / BLACKOUT / STORM; snakes, arc barriers, vortexes, multi-arm gates, a boss every 4th wave, and gun / shield / life power-ups.
* **Neon Arena shop**: geoms are *tokens* (shown in the HUD, top left). **X** pauses the game and opens the upgrade shop (W/S select, Space/A buy, X/B/Enter close; not in demo mode). Add-ons: FIRE RATE (6 levels - the game now starts slow and a maxed rate is the old fast rate), THRUSTERS, GUN TYPE (Mk I-IV: twin, side guns, wide fan, piercing), MISSILES (auto-homing), MINES (auto-laid), COLLECTOR (magnet), PROBES (two roaming hunter drones). The screen lists each component's level, current and next-level stats and price. Data: `kNeoUps`, `NeoUpStat`, `NeoShop*`. The smart bomb moved to B / Y / Shift.
* **Level-name banner**: now drawn like the DEMO banner (pure vector strokes, no plate) at the same spot, sized so the whole name fits. The vector font (`VectorGlyph`) now has the full alphabet; before, missing letters were silently skipped.
* **Radical, larger levels (Neon Arena and Scoptiliptic)**: seven new fields - VORTEX (rotating spiral arms), CANYON (11-high walls), MOUNTAINS (sharp crossing ridges), COLISEUM (stepped tiers), GEAR TUBE (six-lobed tube), CORKSCREW (winding tube), HOURGLASS (a tube that pinches to 35 %). They are always huge, half of all levels from wave 3 are radical, floors grow faster (+4.5 x 3.0 per level, up to 130 x 90; GRAND up to 180 x 110) and half the floors from wave 2 are GRAND. See `ArShape`, `ArHeight`, `ArSurf`, `ArConfigure`.

## Recent gameplay changes (pause banner, Scramble crash, abductors, mutant UFO)

### PAUSED banner (every scene)
`DrawPauseBanner` (just before the level-name banner in `main.cpp`) replaces the old plain "PAUSED" text. "PAUSED" is drawn in the same vector strokes as GAME OVER, centred on the screen; **each letter spins about the horizontal (X) axis** at its own speed and phase (its y coordinate is rotated and shrunk by a little perspective). When the game resumes the banner **slides off the screen in a random direction** (chosen at that moment) and accelerates as it goes. Pausing again while it is still leaving brings it straight back. It is not drawn while the options window is open (that has its own message).

### Scramble: running out of fuel
`ScrambleGame::crashing` / `crashVy`. At zero fuel the ship loses power instead of exploding: control, firing and bombs are off, it falls with gravity (nose dropping, rolling over, trailing sparks) and is destroyed only when it reaches the terrain (the existing ground check). Reset on respawn, new game and `ScKillShip`.

### Guardian / Voxel Guard: one abductor per human
A lander (Guardian) or abductor (Voxel Guard) now skips a human another one has already claimed (its `target`, which it keeps while hunting, descending and carrying). If every standing human is claimed it loiters near the top and asks again each frame; it only turns into a mutant when no human is left standing.

### Voxel Guard (Resogun): enemies, bosses, spire and floor
All in `main.cpp`, search `VoxType`, `VoxBoss`, `VoxFoeShot`.

**Floor and people.** The ground's top is lower (`VoxelGame::groundTop = -9.0`: a low strip along the bottom of the screen). Its colour is light brown in the first wave set and a different random entry of `kVoxGroundPal` in each later set (waves 1-5, 6-10 ... `kVoxSetLen`). Standing people are drawn on the **middle** of the floor's top face instead of its front edge (the draw code shifts them 1.2 along the surface normal; the shift fades with height so a falling or carried person slides into the middle as they come down).

**Enemy types.** The first five are the originals (abductor, bomber, swarmer, guardian, mutant). `kVoxIntro` says when each new type first appears; it then adds one more every two waves up to a cap, and a "NEW: ..." notice shows on its first wave:

| Type (wave) | Behaviour and firing |
|---|---|
| Pod (4) | Slow drifting capsule; never fires. Shot, it bursts into four swarmers. |
| Sniper (5) | Keeps 12-16 units off; flashes white for 0.7 s, then fires a fast **rail** slug aimed at where you are. Every second volley is a double-tap (two parallel slugs). |
| Miner (6) | Cruises at mid height: two floating **mines**, then a **homing heat-seeker**, repeating. |
| Lancer (7) | Prowls the top; flashes and drops to your height (the cue to move), then charges straight across the screen at 26 units/s and climbs back. |
| Sprayer (8) | Keeps 8-12 units off at your height; fires **pellets** in turn: narrow fan of 3, wide fan of 5, slow ring of 8. |
| Phantom (9) | Chases you, blinks out for 1 s (cannot be hit, cannot hit you, not drawn), reappears beside you with a ring of six pellets. |
| Carrier (10) | Armoured (10 hits; a smart bomb only does 3, like the guardian): alternately launches two swarmers and fires a pair of homing missiles. |
| Reaper (11) | Hunts standing people (one reaper per person), dives and executes them. With nobody left it chases the ship. |

**Shot kinds** (`VoxShotKind`, stored per slot in `VoxelGame::foeKind`, always written by `VoxFoeShot`): bomb (the original), rail (long cyan streak), mine, pellet (small yellow), homing missile. Your shots destroy mines and homing missiles (+25). The enemy shot pool is 40 (`kVoxMaxShots`) and the foe pool 96 (`kVoxMaxFoes`).

**Spire.** It has four styles, chosen by wave set: twisted tower, stepped ziggurat, crystal spire with orbiting shards, pylon gate with turning discs.

**Bosses.** Wave 5, 10, 15 ... (`g.wave % kVoxSetLen == 0`) is a boss wave: the boss and three abductors. Bosses alternate **the Rolling Wheel** and **the Stomper** (a giant walking robot) and have more hit points each time (+35 %). Each has four **hot spots** with their own hit points (24 per outer spot, 48 for the core, scaled): the wheel's three rim nodes and hub, or the robot's reactor, two shoulder cannons and head. A spot **flashes white-yellow for 0.14 s** when hit, is a dark husk when destroyed, and the core (hub / head, blue while shielded, red when open) can only be damaged once the three outer spots are gone; shots on the armour or the shielded core are absorbed. Killing the core kills the boss. The wheel rolls toward where you were (re-aiming every 3.5 s), hops and crashes down with ground shock-waves, rolls over people, and fires pellet fans below 75 % health. The Stomper walks toward you; each stomp kills people under the foot, every third also sends two ground shock-waves, and each live cannon fires aimed bolts. Below half health both speed up. Touching the armour destroys the ship. A smart bomb does 4 to each open spot. The HUD shows a BOSS health bar and a red radar blip. The autopilot targets the outer spots and keeps clear of the armour.

### Score awards: extra lives and bombs in the arcade games
One rule for all of them (the same one Gyrospace uses): an **extra life at 10,000, at 50,000 and then every 100,000 after that** (150,000, 250,000 ...), and a **free bomb at every 50,000** in the games that have player bombs. Lives and bombs top out at 9. A life plays `powerup.wav` (`kSfxPowerup`) and a bomb plays `ma-bonus.wav` (`kSfxMiBonus`); each shows an "EXTRA LIFE" / "NEW BOMB" notice for 2 s, drawn by the standard HUD (`DrawGameHud`, from `g_awardMsg` / `g_awardT`).

It is implemented once, in `ApplyScoreAwards` (just above `GameDecention` in `main.cpp`): the games keep their score, lives and bombs in different structs, so `AwardRefs` maps a **scene number** to pointers to those three fields and the function runs once a frame after the games have updated (in the scene `switch`'s caller). The games themselves needed no changes. Each scene keeps its own next thresholds (`g_award[scene]`); when a game's score falls (a new game began) the thresholds start again. Nothing is awarded while `lives <= 0`.

| Scene | Game | Bombs? |
|---|---|---|
| 5 | Asteroids | no |
| 6 | Guardian | yes (smart bombs) |
| 7 | Scoptiliptic | no |
| 8 | Scramble | no (its bombs are a weapon, not a count) |
| 9 / 14 | Chomper / Chomper 3D | no |
| 11 | Neon Arena (Geometry Wars) | yes |
| 13 | Voxel Guard (Resogun) | yes |
| 15 | Marble Madness | no |
| 16 | Jumpman | no (its bombs are things to defuse) |
| 18 | Dungeon (Gauntlet) | no |
| 19 | Spiral Tunnel | no |
| 20 | Morpheous | no |
| 22 | Project X | no |
| 25 | Stix | no |
| 27 | Battle Zone | no |
| 28 | Tutankham | yes (flash bombs) |
| 30 | Burger Master | yes: its "bombs" are **pepper shakes** (a free shake every 50,000; the notice reads NEW PEPPER) |

Gyrospace (scene 17) has its own copy of the rule (`GyAwardScoreBonuses`). Battle Zone's and Tutankham's older extra-life rules (`nextExtra`) are switched off so they do not double up with this one; Tempest, Decention, Super Stardust, Trench Run, Incoming and the other games are not covered. To add a game, add a `case` to `AwardRefs`.

### High-score name entry: the first letter now types

Two things used to eat the first letters of a name. (1) The entry shared `KeyEdge()`'s key memory, which is only refreshed when a key is polled, so letters polled elsewhere (the overlay's Alt+D / Alt+O / Alt+H hotkeys read D, O and H every frame, and some games poll letters) lost their first press, and a key held from play (WASD) could type itself. (2) The overlay's scene hotkeys ran first: **P**, **C** and the **digits** were taken as pause / auto-cycle / scene switch instead of name characters. Now `NameEntryUpdate` keeps its own edge state (keys held when the screen opens must be released first; every other key registers its first press), Fire / Bomb / Start are ignored for the first 0.35 s (the player is usually still mashing them), and the overlay's pause / scene / auto-cycle hotkeys are off while a name is being typed (Esc still quits).

### Stix fuse: lights when you stop, burns about a second later
When the marker stops while drawing, the fuse now works in two phases (`kStxFuseLight`, `kStxFuseDelay` in `main.cpp`): it **lights** once the marker has been still for 0.25 s (a spark sits at the start of the line, as a warning), and about **one second later** (1.25 s after stopping) it starts to **burn along the line toward the marker**. A step still pauses it where it is, and the delay applies again after the next stop. (It used to light after 0.6 s and start burning immediately.) The demo autopilot's idea of how long it has before the fuse arrives (`burn` in `BotStix`) includes the delay.

### Stix demo autopilot: no more stalling on the line
`BotStix` (`main.cpp`) used to stand still on a half-drawn line whenever a sparx could reach the line's end first OR the Qix was near the path, for as long as the fuse allowed - so it often froze until the fuse almost caught it. Standing still is now a last resort with a hard limit per line (`stallT`, reset when no line is being drawn): it waits at most **1.5 s only when truly trapped** (a sparx can arrive at the end AND the Qix is close to the path), only **0.3 s** for a single threat, and never when the fuse is about to reach it; after that it commits and finishes the line.

### Mutant UFO (Guardian)
Mutants are no longer the spiky urchin: `BuildMutantUfoMesh` (VectorShapes) is an armoured, razor-rimmed saucer with six blade-spikes, six fangs, a red-glowing emitter, red rim slits and an amber eye band; `BuildMutantUfoRingMesh` is a ring of eight hooked teeth. In `main.cpp` the hull spins fast about its vertical axis, leans toward the player, pulses crimson to red-orange, and the ring counter-spins 1.7x faster. `g_mutant` (the urchin) is still used by Scramble's divers.

### Chomper / Chomper 3D: glowing eyes on the way home
An eaten ghost (`mode == 3`) is drawn as the eyes mesh. `BuildGhostEyesMesh` now marks its faces self-lit (`glow`), and `SubmitGhostEyeGlow` (above `SubmitFruit` in `main.cpp`) adds a pulsing, shimmering cyan-white halo round each eye while the eyes travel back to the cage. Used by both the flat and the 3D maze.

### Chomper / Chomper 3D: a different wall style every wave
`PacGame::wallStyle` is picked at random in `PacNewLevel` (never the same as the previous wave) and used for **every** wall of that wave, so each wave has one consistent look. `PacSubmitWall` (next to `PacLit` in `main.cpp`) builds them: 0 blocks (the original cubes, with the four-block light fade), 1 boulders (20-face asteroid meshes, each turned and sized slightly differently), 2 crystals (tall octahedron spires with a small companion), 3 bubbles (two stacked glowing spheres that bob), 4 diamond pillars (cubes turned 45 degrees with a spinning cap), 5 spears (fat upright arrows). All styles are shaded by Chomper's light (`PacLit`) and used by both the flat and the 3D maze; each wall is one to three meshes, nothing allocated. The first wave never starts with plain blocks (it picks a style other than the default 0).

### Scene changes fade
Every scene change (number keys, F-keys, Ctrl+F1-F8, PgUp / PgDn and the demo scenes' auto-cycle) now goes through `requestScene` in `Overlay`: the screen fades to black over 0.30 s; at full black the scene index is switched - the new game is initialised that very frame, unseen (`sceneEntered`) - and the picture fades back in over 0.45 s. Requests made during a fade are ignored. The black layer is the last thing drawn (`fadeA`).

### Chomper wall styles (update)
Ten styles now (`kPacWallStyles`): blocks, boulders, crystals, bubbles, diamond pillars, spears, totems, shard clusters, domes and step pyramids. Every non-block style stands on a low plinth that fills its tile, so neighbouring walls join into continuous lines and the maze stays readable (loose spheres and spires left gaps that made the maze look broken).

### Chomper: the lower half of the maze vanished (face budget)
Cause: the renderer keeps at most `kMaxPolys` visible faces per frame and silently drops every face submitted after that. The sphere wall styles (3 bubbles: two 80-face spheres per wall, about 85 visible faces; 8 domes) overflowed the old 8192 on level 2 and later, and since the maze is drawn row by row the walls AND pellets of the bottom rows were the ones lost. Fix: (1) `kMaxPolys` is now 16384 (buffers are reserved once; only submitted faces cost time); (2) `PacSubmitWall(..., cheap)`: for styles 3 and 8, walls farther than 5 tiles from Chomper are drawn as the plinth plus one octahedron in the style's colour (about 9 faces) instead of the full model (both the flat and the 3D maze). If a scene ever loses its last-drawn objects again, suspect this budget first.

### Scoptiliptic (was Omega Race)
The game is now called **Scoptiliptic** (title, banners and docs). **Barrier drones bounce**: their velocity is recomputed every frame (pair drift or hunt), so a wall ricochet used to be lost at once; `Droid::bvel / bounceT` now make a drone that hits a wall fly its bounced heading (turned a little at random) for 0.9 s and turn its weave phase round, then the pair behaviour resumes.

## MyBlastStyles - ten signature kill explosions

A separate pool (own particles, never shared with `MyCubeExplosion` / `MyExplosionFX`). Styles: Firework, Starburst, Vortex, Shatter, Bloom, Collapse, Confetti, Lightning, Gyro, Pixelate.

```cpp
static MyBlastStyles bs(220);
bs.SetDensity(g_vq.particles);                                   // quality scaling, as for the other explosion classes
bs.Spawn(bs.Pick(mask), pos, normal, 3.0f, 255, 160, 60, vel);    // mask: bit (1 << style); Pick never repeats the last style if it has a choice
bs.Update(dt, gravity);                                          // use zero gravity in top-down views
for (size_t i = 0; i < bs.Capacity(); ++i) {                     // draw when life > 0 and delay <= 0, by p.type:
    const MyBlastParticle& p = bs.At(i);                         // Ember (sphere), Shard (plate/cube), Bolt (streak), Ring, Orb (translucent shell)
}
```

In `main.cpp`, `Boom()` spawns one for medium/big kills (about 70 % of kills with n >= 12, and always alongside the Nova for n >= 36). `BlastMaskForScene()` chooses the allowed styles per game type, and `SubmitBlastStyles()` draws them from `SubmitFx()`.

### Guardian: a freed human is never killed by the fall
Killing (or ramming) a lander that is carrying a human releases the captive: it starts to fall from rest, and landing always leaves it alive and walking again (before, a drop of roughly 4+ units of speed killed it). It dies only if a player bullet actually hits it while it falls, and you can still catch it with the ship.

### Guardian: lost bullets, flicker and the first-shot freeze (fixes)

* **Shots vanished near the world seam.** Player shots were spawned at the ship's raw ring coordinate, but hit tests and drawing use the camera-relative x; when the camera and ship were on opposite sides of the seam the shot was a whole world away and invisible. Shots now spawn camera-relative and ride across the seam with the camera (player and enemy pools, like the explosion particles).
* **Shots could be culled by the vector quality LOD** (a 0.09-thick bolt is under the cull size at Low / Auto-under-load). Shots are drawn with `keepShape`. A full player pool recycles the oldest shot (`SpawnBulletReplace`) instead of ignoring the trigger.
* **First-shot freeze.** The ~100 sound effects were registered on the game thread at the first play (about 1.5 s with fire ignored). `SfxWarmup()` now registers them on a worker thread when the demo starts; a sound requested before it finishes is skipped, never waited for.
* **Explosions.** `g_boomIntensity` (Guardian sets 0.6 for its frame) scales debris count, fireball size and how often a signature style is used. Explosion drawing stops at `kFxPolyBudget` (75 % of `kMaxPolys`), so a big blast can no longer push ships and shots past the face cap, where the overflow was silently dropped (flicker).
* Landers are drawn in place; the shaken draw copy is only made for frenzied ships and mutants.
* **Shadow flicker.** A land pinned to the back (`drawFirst`) now gets its terrain shadows at an explicit depth (`g_tshadow.depth = kDepthShadow`, Guardian only; Scramble's depth-sorted land keeps real depth). With real depth, a spinning ship's faces changed their average depth every frame and flipped in front of / behind its own shadow. The Vector Shadows switch also has hysteresis (off below detail 0.25, on above 0.35) so the Auto governor stepping around 0.45 no longer pops every shadow on and off.
* **Whole-scene flash once per lap.** `GameGuardian` built the view matrix from last frame's `camX` and then moved `camX` before drawing around the new one. On the frame the camera crossed the ring's seam (90 -> 0) the camera was a whole world away from everything drawn, so land, ships and shots all left the screen for one frame. The camera is now re-aimed right after `camX` is updated.
* **Grouped rescue bonus.** When any clinging human's feet reach the ground, every carried human is set down together; the bonus (500 each) is added up and shown once as a single pop, with one jingle.

* **Auto quality LOD flicker (all games).** `VecQualityUpdate` feeds `lodMul` from a hysteresis copy of the detail level that moves only after the real detail drifts 0.2, so models near the hexagon-swap size no longer flip with every Auto governor step. `keepShape` remains for articulated figures and the cars.

### No enemies spawn near the player
* **Asteroids**: wave rocks appear on the side bands but are re-rolled until at least 7 units from the ship (it can be parked at an edge).
* **Guardian**: a new wave's ships are placed at least 18 units round the world from the ship (`DefFarX`); baiters already spawn 18-30 away.
* **Scoptiliptic / Neon Arena**: every enemy (droids, foes, groups, barrier pairs, wells, the chaser) that would appear within 12 units of the ship is pushed out to 12 (a splitter's children are exempt, they come from the dead parent). Raiders (`SrUpdate`) try other bearings if the arena wall dragged their spawn within 14 of the player.
* Already safe by design: Morpheous (formations 34+ away), Super Stardust (60-110 degrees), Battle Zone (48-62), Tutankham (9-26 tiles), Space Spartans (squadron distance), Project X (right edge).

**Planets.** Jupiter now has its GREAT RED SPOT (an oval 22 degrees south with a pale rim, painted where the map is sampled, so every detail level has it) and the Gallery shows Saturn WITH its rings (`BuildSaturnRingsMesh`: C, B and A rings, Cassini division and Encke gap, tilted, double sided; Gyrospace draws its own lit, shadowed rings in game).

**Animated thrusters.** `BuildFleet` records every engine nozzle (`FleetEngines(id)`: position, radius, built-in exhaust length). `SubmitMesh` now draws a flickering flame cone at EVERY nozzle of any fleet ship (`SubmitFleetEngines`), wherever it is drawn - games, shadowed objects and the Vector Gallery - out of phase per ship and per engine; `SubmitFleetShip` only sets the thrust (0..1) and keeps the single tail flame for craft that are not fleet ships (raider, warship ...). The mine has no engines.

* **Neon Arena pacing**: ships alive at once are capped at 6 + 1.5 per wave (7, 9 ... 13 on wave 5, 21 on wave 10, at most 45; it was 34 + the wave), they arrive every 1.4-2.6 s on wave 1 (shortening slowly with the wave, down to half by wave 21), and barrier pairs and groups only join from wave 3 and less often.

* **Neon Arena shop**: the upgrade shop window is animated and colourful (`NeoShopDraw`): a panel that drops in, a rainbow border running round it with breathing glow, drifting colour bars, twinkling stars and scan-lines, a title with sliding magenta / cyan ghosts and a shining underline, a spinning gold coin and a token count that rolls up, rows that slide in one after another, one colour per component (stripe, pips, name), a cursor with a pulsing glow, a sweeping highlight and a nudging arrow, pulsing affordable prices, shimmering MAXED, a spark burst on a purchase and a red shake on a refusal, and a status line with each figure in its own colour.

The thunderstorm backdrop (far scene 4) draws each cloud as nine shaded puffs piled on a dome plus three flattened puffs for its underside, lit from inside by the lightning flash.


> **Naming note:** the game that used to be called *Defender* (scene 7, `Df*` identifiers in the code, sound files in `Assets/Defender/`) is now called **Guardian** everywhere the player or the documentation sees it. The code identifiers and the asset folder keep their old names so existing files and saves keep working; the arcade sound ROM it was rendered from is still the original Williams game's.

> **Naming note:** *Pac-Man* (scene 10) and *Pac-Man 3D* (scene 15) are now called **Chomper** and **Chomper 3D** in the scene titles, the Vector Gallery and these docs. Code identifiers (`Pac*`, `kSfxPac*`, `pac-*.wav`) are unchanged.

> **Naming note:** *Missile Attack* (scene 25, `Mi*` identifiers, `ma-*.wav` sounds) is now called **Incoming**.

## Hall of fame shows, enemy fire on death, Neon Arena probes (update)

* **Hall of fame** (`DrawHallOfFame`, main.cpp): each showing picks from its seed one of 8 backdrops (`DrawHallBackdrop`: neon floor, tunnel, aurora, digital rain, ocean, galaxy, skyline, sunburst) and one of 8 arrival animations for the five lines, over the **three-layer streaming star-field** (`DrawHallStars`, the Space Spartans layers: far / middle / near at 8 / 20 / 46 units a second, near stars streak; a star re-enters at the far end at a new place) and **fireworks with real ballistic physics** (`DrawHallFireworks`: a climbing rocket with a spark trail, then a spinning 3D shell of sparks with drag-and-gravity closed-form motion, perspective scaling, seven shell types and a finale salvo). All stateless functions of the time and the seed (`HsRand`), no allocation. The name-entry screen shares the stars.
* **Enemy fire vanishes on death** (`ClearEnemyFireOnDeath`, next to `ApplyScoreAwards`, called once a frame): when a scene's lives drop, that game's enemy bullet arrays (and `SrClearShots()` for the special raiders) are cleared in the same frame.
* **Neon Arena probes** (`NeoProbe`: `mode`, `tgt`, `think`): they choose prey by priority within `kNeoProbeLeash` (26) of the ship, spread across two victims, fetch geoms when no enemy is in reach (touching one collects it), and return to orbit the ship when nothing is near or they stray beyond the leash.

## Performance pass (update)

Made without a profiler or a build - each item removes work that is plainly done on every frame, and none changes what is drawn (except where stated).

* **Painter's sort** (`SortKeysFarToNear`, main.cpp): the per-frame depth sort of up to 32,768 polygons is now a stable LSD **radix sort** (three passes over a packed 64-bit key, preallocated buffers) instead of `std::sort` with a two-field comparator; frames under 512 keys keep `std::sort`. Equal depths still keep submission order.
* **Chess / 3D Chess / Xiangqi engine** (`BgSearcher`, BoardCore.inl): every search node used to zero-fill about 4 KB (`BgMove mv[512]; int sc[512]`, and two arrays in the quiescence search, because `BgMove` has default member initialisers). Each ply now owns a slice of two buffers allocated once per search, so nodes allocate and initialise nothing.
* **Renderer** (`Renderer::DrawFilledEllipse`, `DX11Renderer`): the opponent portraits draw hundreds of ellipses a frame, each of which created a Direct2D path geometry. DX11 now fills a native ellipse (rotated through the render target's transform); other backends use the base class's polygon, exactly as before. `ChEll` calls it. `DX11Renderer::DrawCircle` no longer creates a brush per call (it recolours the shared brush).
* **Hall-of-fame fireworks** (`HsPush*`, `HsBatchFlush`): the sparks, tails and halos of a burst are collected, colour-quantised (3 bits a channel and alpha), counting-sorted and drawn as one `DrawFilledPolygonBatch` per distinct colour - a few dozen calls for a whole sky instead of thousands of single polygons.
* **Text**: `TextF`, `ChText` and `ChTextC` reuse one `std::wstring` instead of constructing a temporary (a heap allocation for any label over 7 characters) per call.

Ideas not done (need profiling first): batching the other 2D HUD polygons per colour inside the renderer, caching portrait thumbnails, quantising the other overlays' polygon calls the way the fireworks do, a transposition table for the engine.

## Anti-aliasing in the vector layer (update)

The Video settings' **Anti-Aliasing**, **MSAA** and **MSAA Samples** reach the 3D pipelines through their rasteriser and multisample targets, but the vector scenes are Direct2D polygons, which MSAA cannot touch (Direct2D draws on a single-sample surface) and which were forced to ALIASED. `VectorAA.h` now applies the settings there (DX11 and DX12 renderers; Vulkan and OpenGL draw their 2D polygons in the multisampled pass already):
* **Type** - Anti-Aliasing OFF: every vector fill, circle and ellipse is aliased (crisp, fastest). ON: per-primitive anti-aliased edges (smooth silhouettes).
* **Level** - MSAA ON adds a hairline stroke in the fill colour after each fill, which covers the seams anti-aliasing opens between neighbouring faces of a model; its width grows with the sample count (2x 0.5 px, 4x 0.8 px, 8x 1.1 px). Direct2D has a single analytic AA quality, so the sample count controls the seam coverage rather than a sample grid.
* Not yet covered: text anti-aliasing (it keeps Direct2D's default) and rectangles (axis-aligned, nothing to smooth).

## Anti-aliasing now covers everything, in every renderer (update to the section above)

`VectorAA.h` is shared by all four renderers, and the Anti-Aliasing switch now **turns everything on or off**:
* **Direct2D layer (DX11, DX12, Vulkan on Windows):** `VectorApplyD2DAA()` runs right after every `BeginDraw` and sets both the shape antialias mode and the **text** antialias mode: ON = per-primitive AA shapes (polygons, circles, ellipses, rectangles, lines and curves) and grayscale-AA text; OFF = aliased shapes and aliased text. It runs every frame, so changing the option takes effect at once. The polygon and batch fills of all three renderers follow the same switch (the old forced "aliased" is gone) and, with MSAA on, add the same-colour seam-cover hairline whose width grows with the sample count (2x 0.5, 4x 0.8, 8x 1.1 px).
* **OpenGL:** its 2D polygons are real triangles in the multisampled framebuffer, so MSAA (chosen sample count) smooths them; with AA on but MSAA off, each polygon gets a smoothed (GL_LINE_SMOOTH) one-pixel outline in its colour; with AA off line smoothing and the outline are off and no multisample format is requested. `DrawFilledPolygon` also became one buffer upload and one triangle-fan draw (it was an upload and a draw per triangle; the 2D vertex buffer now holds 32 vertices).
* **3D pipelines** keep taking the same settings through the rasteriser / multisample targets (unchanged).

## Performance profiling for the vector system - Alt+P (new)

The counterpart of the 3D engine's F12 timing capture. **Alt+P** switches recording on and off in any vector scene (plain P still pauses). While it is ON every frame's timings go into a ring buffer of the last 3,600 frames (`g_vpBuf`, a fixed array, one small store per frame): the **frame** time between two overlay calls (everything: the renderer's 3D pass, Present / vsync, the overlay), the **game** code, the painter's **sort**, the polygon **fill**, the scene's **HUD** pass, the whole **overlay** (so `outside = frame - overlay` is the renderer / Present share), and the polygon, draw, LOD and culled counts. Switching it OFF dumps the recording: a CSV `Assets/Profiling/profile-YYYYMMDD-HHMMSS.csv` (one row per frame) and a log summary (average / min / 95th / 99th percentile / max of every stage and the five worst frames with their scene numbers). A small vector banner **PERFORMANCE PROFILING: ACTIVE / OFF** rises from below the bottom edge while fading in, holds and fades out on every toggle (`DrawVecProfBanner`); while recording a pulsing red dot and "PROFILING" stay in the bottom-right corner. Code: `VecProfToggle / VecProfRecord / VecProfDump` before `Overlay` in main.cpp.

## Scoptiliptic's rubber walls follow the floor (update)

The barrier **lanes** are now one continuous swept ribbon (top face plus two side faces) built between surface frames (`ArSurf`) taken every 0.7 units along the lane, each with its own surface normal and tangent, instead of a row of straight 2-unit boxes that stood at angles to each other on uneven fields; the pink field **rails** (`ArDrawGrid`'s new `railSeg` parameter, 0.7 for Scoptiliptic, 3.0 elsewhere) are cut finer so they bend with the floor too. Neon Arena's rails keep the old segmentation.

## Thirteen more kill explosions: 23 styles in all (update)

`MyBlastStyles` (Vectors.h / Vectors.cpp) gained **Supernova** (a huge white flash, two tilted shock rings, long rays, a far shower of hot embers), **Meteors** (streaks fanning down with ember trails, staggered), **Fountain** (a flat base ring and a stream of embers jetting up and arcing down), **Mushroom** (a rising stem, a billowing cap of two swelling orbs and a flat ring, a ground shock ring), **Snowflake** (six symmetric ice arms with two levels of side branches), **Galaxy** (three slow spiral arms round a bright core), **Petals** (eight curling petals of embers), **Pulse** (five thin sonar rings one after another), **Fireflies** (slow wandering glowing motes), **Flame** (a directed fire jet along the normal, white-hot to red, rising), **NovaCross** (a lens-flare cross), **CubeCascade** (heavy tumbling blocks that fall) and **Helix** (a double helix of embers). They use the existing particle types, so `SubmitBlastStyles` draws them unchanged. `BlastMaskForScene` (main.cpp) lists the styles that suit each game (bits 10-22 for the new ones), the pool grew from 220 to 360 particles, and the Vector Gallery lists **all 23** under KILL BLAST.

## Hall of fame performance pass (2026-11-12)

Profile (`Assets/Profiling/profile-20261112-164815.csv`, a game scene): ~12.5 ms of overlay with the game alone, ~32-38 ms once the hall of fame was up, with the polygon count unchanged, so the table's 2D drawing cost ~20 ms a frame. Cause: `DrawHallText` issued one Direct2D path geometry (`HudSeg`) per stroke per pass per letter, about 1,500 a frame.
* **`DrawHallText` is batched**: each pass (glow, outline, body, bevel) of a whole string is gathered with `HbSeg` and drawn by `HbFlush` as one `DrawFilledPolygonBatch` per colour run (`g_hbPts` ... static storage; 512 quads or a colour change flushes). Passes now run string-wide, which also stops a letter's glow from drawing over the previous letter. Helpers: `HbQuad`, `HbSeg`, `HbRect`, `HbFlush` (just above `DrawHallText`).
* **Backdrops**: the star-field uses the fireworks' spark batcher (`HsPushSquare` / `HsPushSeg`, colour quantised to 3 bits a channel); the ocean waves, neon floor lines, tunnel squares and city windows use the `Hb*` batch.
* **Hidden game not filled**: once the hall (or the name-entry screen) has been up 0.6 s, its 98 % opaque sheet covers the frozen game, so the game's `g_hudPre` background and the filled 3D faces are skipped (`hallCovers` in the frame loop). The game's update, mesh submission and HUD callback still run.

### Audio and award-banner completion (2026-11-13)

The sound bank has 204 definitions, checked at compile time; the Soldier On pack
checks its next-bank boundary rather than the total. New cues cover Neon Arena's
tokens/mines/probes, Morpheous shield absorption, Jumpman movement/climbing/landing,
and Dungeon Crawl shield/death transitions. Chomper 3D already shares all Chomper
logic and sounds. Guardian's existing mapping is retained.

EXTRA LIFE uses 4.5% of window height and at most 45% of width; slide/bounce/glow
remain, using at most three batched draws. Local GameSFX footfall conversions are
mono 16-bit 22,050 Hz PCM, with hashes in `Assets/Arcade/footsteps-provenance.json`.
Run `python Scripts/validate-vector-audio.py` for non-build bank/asset checks.

## Depth fade (2026-11-14, uncompiled)

`g_df` (main.cpp, beside `g_vq`) hazes distant **world polygons** (ground grids, floors, terrain tiles; not meshes) toward the scene's background colour so far ground recedes instead of ending in a hard cut. It calibrates itself: the fade runs from half to all of the farthest polygon depth of the previous frame (smoothed, capped at 600), so no per-game distances are needed, and an oversized clipped floor only weakens it. Strength is 55% at the far edge, quantised to quarter steps so faded colours still batch. It is enabled per frame for the plain dark-backdrop 3D scenes (Neon Arena 11, Trench Run 12, Tempest 26, Battle Zone 27) and only while **Vector Effects** is on. To enable another scene set `g_df.maxK` (and `g_df.col` if its backdrop is not near-black) in the frame set-up beside `g_polys.clear()`. Soldier On and Racing keep their own fog and lighting.


## Super Stardust explosions follow the planet (2026-11-14, uncompiled)

`StarBoom` (main.cpp) replaces `Boom` for every explosion on the planet. It scales the blast by its camera distance relative to the point under the ship (`d0 / d`, clamped 0.7 - 1.7, through the new `g_boomScale`, which `Boom` applies to the debris size / speed and the fireball size), so near blasts are bigger and far-side ones smaller on top of ordinary perspective. When the planet rolls, `StarRollParticles` rotates every live particle of the three shared systems (`MyCubeExplosion`, `MyExplosionFX`, `MyBlastStyles`) about the planet's centre with the same rotation, through their new `Rotate(q, centre)` members (positions, velocities, ring normals, spin axes and cube orientations), so a blast stays on the spot of ground where it happened instead of hanging in the world.

See [Arcade setup, score/time records and Player Play-back](Arcade-Playback-Example-Usage.md) for the current shared controls, recording retention, native validation and rendering changes.

## Mesh tools and the procedural ship generator (merged 2026-11-19, uncompiled)

In `VectorShapes` (namespace `MyShapes`):
* `Mesh BevelMesh(const Mesh& src, float inset = 0.10f, float lift = 0.010f)` - a rounded-edge twin of any mesh: each large face gets an inset, raised inner copy and one chamfer quad per edge whose normal leans outward, so the lighting rolls off round every edge. About four times the faces: draw it near the camera and the plain mesh far away.
* `void FindStrobePoints(const Mesh&, MyVector& port, MyVector& starboard, MyVector& top)` - the most port, most starboard and highest vertices (wing tips and fin) for navigation / strobe lights.
* `void BuildProceduralShip(int design, int variant, unsigned seed, Mesh& out, std::vector<FleetEngine>& engines)` - ten designs (needle, delta, twin-boom, wedge, gunboat, carrier, hauler, interceptor, cruiser, saucer) in four variants; the hull is lofted from superelliptic sections, with a canopy, swept tapered wings and gun barrels, nacelles or cargo pods, and glowing nozzles registered in `engines` so a renderer can animate a flame at each. Deterministic for a seed.
