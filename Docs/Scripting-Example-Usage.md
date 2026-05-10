# ScriptManager — CPGE Game Script (.cgs) Reference
**Engine version:** v1.0  
**Document written:** 2026-05-10  
**Author:** LordAries1972

---

## Table of Contents
1. [Overview](#overview)
2. [Script File Format](#script-file-format)
3. [Versioned Header](#versioned-header)
4. [Command Reference](#command-reference)
   - [Execute](#execute)
   - [QUIT](#quit)
   - [ALERT](#alert)
   - [STOP](#stop)
   - [POSITION](#position)
   - [PLAY_POSITION](#play_position)
   - [GET_READY](#get_ready)
   - [RESET](#reset)
   - [SAVE](#save)
   - [LOAD](#load)
   - [DETECT_COLLISION](#detect_collision)
   - [WAIT](#wait)
   - [LABEL](#label)
   - [GOTO](#goto)
5. [WAIT](#wait)
6. [LABEL](#label)
7. [GOTO](#goto)
8. [Execute Function Index](#execute-function-index)
6. [Integration Guide](#integration-guide)
7. [Script Loading at Scene Start](#script-loading-at-scene-start)
8. [Error Handling](#error-handling)

---

## Overview

The ScriptManager loads, parses and executes plain ASCII scene scripts stored as
`.cgs` files (CPGE Game Script).  Each scene may have exactly one script file
located at:

```
Scripts/<SCENE_NAME>.cgs
```

Scripts are detected and loaded automatically during scene initialisation.
If no script exists for a scene that is fine — the ScriptManager silently skips
loading and the scene runs with engine defaults only.

Scripts can run synchronously (blocking) or asynchronously (background thread).
Per-frame collision rules registered via `DETECT_COLLISION` are evaluated every
frame via `ScriptManager::Update(deltaTime)`.

---

## Script File Format

Scripts are **plain ASCII / UTF-8 text files**.  Each file begins with a
versioned header block followed by the command body.

```
##CPGE_SCRIPT
##ScriptVersion: 1.0
##Written: 2026-05-10
##Scene: SCENE_GAMETITLE
##Author: LordAries1972
##Description: Short description of what this script does.

# This is a comment line — ignored by the parser
COMMAND arg1 arg2 "quoted string"
```

| Rule | Detail |
|------|--------|
| Header lines | Must start with `##` |
| Comment lines | Must start with `#` |
| Blank lines | Ignored |
| Commands | Case-insensitive keyword, one per line |
| String arguments | Wrap in `"double quotes"` to preserve spaces |
| Line endings | CRLF or LF both accepted |
| Encoding | ASCII / UTF-8 (no BOM required) |

---

## Versioned Header

Every `.cgs` file **must** begin with the following header fields (order
does not matter, but all should be present):

```
##CPGE_SCRIPT
##ScriptVersion: 1.0
##Written: 2026-05-10
##Scene: SCENE_GAMETITLE
##Author: YourName
##Description: What this script does.
```

| Field | Purpose |
|-------|---------|
| `ScriptVersion` | Script language version (currently `1.0`) |
| `Written` | ISO date the script was authored (`YYYY-MM-DD`) |
| `Scene` | Scene this script belongs to (matches `SceneType` enum name) |
| `Author` | Author of the script |
| `Description` | Free-text description |

When you revise a script, **update the `Written` date** to reflect when the
revision was made.  This is the script's version stamp.

---

## Command Reference

---

### Execute

```
Execute FunctionName(arg0, arg1, arg2, ...)
```

Calls a registered engine API function.  Function names are
**case-insensitive**.  Arguments are comma-separated inside the parentheses.
Boolean arguments accept `true`/`false`, `1`/`0`, or `yes`/`no`.

**Examples:**

```
Execute FadeToBlack(2.0, 0.5)
Execute CreateStarfield(300, 450.0, -600.0, 0.0, 0.0, 0.0, false)
Execute JumpTo(10.0, 2.0, -5.0, 2, false)
Execute SetGlobalVolume(0.85)
Execute SetPlayerState(0, 1)
```

See the [Execute Function Index](#execute-function-index) for all available
functions.

---

### QUIT

```
QUIT
```

Requests an immediate, clean engine shutdown via `PostQuitMessage(0)`.
All running script execution is halted first.

**Example:**

```
ALERT CRITICAL "Fatal initialisation failure — shutting down."
# CRITICAL alert automatically calls QUIT — explicit call below is redundant
# but valid for clarity:
QUIT
```

---

### ALERT

```
ALERT <severity> "<message>"
```

Creates a centred red-background alert window with yellow text.

| Severity | Colour | Auto-QUIT |
|----------|--------|-----------|
| `General` | Red window, yellow text | No |
| `Error` | Red window, yellow text | No |
| `CRITICAL` | Red window, yellow text | **Yes — triggers QUIT** |

**Examples:**

```
ALERT General "Loading complete."
ALERT Error "Failed to load texture asset."
ALERT CRITICAL "Memory corruption detected — engine cannot continue."
```

---

### STOP

```
STOP <target>
```

Halts a specific engine subsystem.

| Target | Effect |
|--------|--------|
| `MUSIC` | Calls `SoundManager::StopPlaybackThread()` |
| `EFFECTS` | Calls `FXManager::StopAllFX()` |
| `MOUSE` | Sets `ScriptManager::IsMouseStopped() = true` — checked by main loop |
| `GAMEPAD` | Sets `ScriptManager::IsGamepadStopped() = true` — checked by main loop |

**Examples:**

```
STOP MUSIC
STOP EFFECTS
STOP MOUSE
STOP GAMEPAD
```

> **Integration note:** For `STOP MOUSE` and `STOP GAMEPAD` to take effect,
> the main render/input loop must query `scriptManager.IsMouseStopped()` and
> `scriptManager.IsGamepadStopped()` and suppress processing accordingly.

---

### POSITION

```
POSITION <x> <y> <z> <yaw> <pitch>
```

Moves the camera to the specified world position and orientation instantly.
All values are floating-point.  Yaw and pitch are in degrees.

**Example:**

```
POSITION 0.0 4.0 -12.0 0.0 -10.0
```

---

### PLAY_POSITION

```
PLAY_POSITION <x> <y> <z>
```

Sets the 3D (and 2D) world position of all currently active players.
Use `Execute SetPlayerState(playerID, state)` to activate/deactivate
individual players before calling this command.

**Example:**

```
PLAY_POSITION 0.0 0.0 0.0
```

---

### GET_READY

```
GET_READY
```

Plays the "get ready" audio cue for the current player using
`SoundManager::PlayImmediateSFX`.

> **Note:** Currently mapped to `SFX_BEEP` as a placeholder.  Add
> `SFX_GET_READY` to the `SFX_ID` enum and update `Cmd_GetReady()` in
> `ScriptManager.cpp` when the asset is available.

**Example:**

```
GET_READY
```

---

### RESET

```
RESET
```

Performs a full engine reset back to `SCENE_GAMETITLE`:

1. Clears all active `DETECT_COLLISION` rules
2. Re-enables mouse and gamepad input
3. Calls `FXManager::StopAllFX()`
4. Calls `SoundManager::StopPlaybackThread()`
5. Calls `SceneManager::SetGotoScene(SCENE_GAMETITLE)` then `InitiateScene()`

**Example:**

```
RESET
```

---

### SAVE

```
SAVE
```

Serialises current scene state to `Precache/scene_precache.bin` via
`SceneManager::SaveSceneState()`.  Creates the file if it does not exist.

**Example:**

```
SAVE
```

---

### LOAD

```
LOAD
```

Restores scene state from `Precache/scene_precache.bin` via
`SceneManager::LoadSceneState()`.  Performs verification of the data on load.

**Example:**

```
LOAD
```

---

### DETECT_COLLISION

```
DETECT_COLLISION <typeA> <idxA> <typeB> <idxB> [radius] <action string>
```

Registers a collision pair that is evaluated **every frame** in
`ScriptManager::Update()`.  When the condition is met the `<action string>` is
dispatched as a script command.  Each rule fires **once** by default (one-shot).

| Parameter | Description |
|-----------|-------------|
| `typeA` | Entity type: `PLAYER`, `WALL`, `ENEMY`, `OBJECT`, `ZONE` |
| `idxA` | Entity index (e.g. player ID 0–7) |
| `typeB` | Second entity type |
| `idxB` | Second entity index |
| `radius` | *(Optional)* Sphere collision radius in world units (default: 32.0) |
| `action` | Full command string to execute on trigger |

**Supported pairs (v1.0):**

| Pair | Detection method |
|------|-----------------|
| `PLAYER` vs `PLAYER` | `Physics::CheckSphereCollision` on `position3D` |
| `PLAYER` vs `WALL` | `GamePlayer::CheckCollisionAtPoint` (collision bitmap) |
| Others | Logged as unsupported — extend `EvaluateCollisionRule()` |

**Examples:**

```
# Alert when player 0 and player 1 are within 48 world units
DETECT_COLLISION PLAYER 0 PLAYER 1 48.0 ALERT General "Players too close"

# Alert on wall hit using default 32-unit radius
DETECT_COLLISION PLAYER 0 WALL 0 ALERT Error "Player hit boundary"

# Trigger QUIT on critical collision (CRITICAL auto-quits)
DETECT_COLLISION PLAYER 0 WALL 0 ALERT CRITICAL "Player fell out of world"
```

---

### WAIT

```
WAIT(seconds)
```

Pauses script execution for the specified number of seconds before the next
command runs.  The wait is interruptible — if `StopExecution()` is called
(e.g. on scene exit), the wait exits immediately rather than blocking shutdown.

Both forms are accepted:

```
WAIT(2.5)       # parenthesised form (preferred)
WAIT 2.5        # space-separated form
```

**Examples:**

```
Execute FadeToBlack(0.0, 0.0)
Execute FadeToImage(2.0, 0.5)
WAIT(2.5)                        # hold for 2.5s before next command fires

PLAY_POSITION 0.0 0.0 0.0
WAIT(1.0)                        # brief pause before the get-ready cue
GET_READY
```

> **Note:** `WAIT` blocks the calling thread.  When using `ExecuteScriptAsync()`
> the wait runs on the background script thread and does not stall the render loop.
> When using `ExecuteScript()` (synchronous), do not call `WAIT` from the main
> thread if it would exceed your frame budget.

---

### LABEL

```
LABEL <Name>:
```

Marks a named position in the script that can be targeted by a `GOTO` statement.
The trailing colon is optional.  Label names are **case-insensitive**.

Labels are resolved at load time — the entire label map is built once after the
file is parsed, so labels may appear anywhere in the file (before or after a
`GOTO` that references them).

Duplicate label names in the same file are an error; the first definition is
kept and subsequent duplicates are logged and ignored.

**Examples:**

```
LABEL TunnelStart:
Execute InitWarpDotTunnel(0.0, 0.0, 0.0, 5.0, 80.0, Clockwise, 3, false, 24, 3)
WAIT(4.0)
Execute StopWarpDotTunnel()
```

---

### GOTO

```
GOTO <Name>
```

Transfers execution to the command immediately following the named `LABEL`.
The label name is case-insensitive.  If the named label does not exist, an
error is logged and execution continues on the next line.

**Infinite-loop guard:** The script engine enforces a maximum of **1,000,000**
executed steps per script run.  If this limit is exceeded (e.g. an unconditional
`GOTO` with no exit path), execution halts and an error is logged.

**Examples:**

```
# Simple loop — restart the tunnel sequence
LABEL TunnelStart:
Execute InitWarpDotTunnel(0.0, 0.0, 0.0, 5.0, 80.0, Clockwise, 3, false, 24, 3)
WAIT(4.0)
Execute StopWarpDotTunnel()
GOTO TunnelStart       # loops back to TunnelStart indefinitely (up to guard limit)
```

```
# Forward jump — skip over a block
GOTO SkipIntro
Execute FadeToBlack(2.0, 0.0)
Execute CreateStarfield(300, 450.0, -600.0, 0.0, 0.0, 0.0, false)
LABEL SkipIntro:
ALERT General "Intro skipped."
```

> **Note:** Conditional branching (jump only if a condition is met) is planned
> for a future version.  Currently `GOTO` is unconditional.

---

## Execute Function Index

All function names are **case-insensitive**.  Arguments shown in `[brackets]`
are optional and have defaults.

### FXManager

| Function | Arguments | Description |
|----------|-----------|-------------|
| `FadeToBlack` | `duration, [delay=0]` | Fade screen to black |
| `FadeToWhite` | `duration, [delay=0]` | Fade screen to white |
| `FadeToImage` | `duration, [delay=0]` | Fade from colour into the scene image |
| `FaderIntoImage` | `duration, [delay=0]` | Alias for `FadeToImage` |
| `FadeToColor` | `r, g, b, a, duration, [delay=0]` | Fade to an arbitrary RGBA colour |
| `StopAllFX` | _(none)_ | Immediately stop all active effects |
| `CancelEffect` | `effectID` | Cancel a specific effect by ID |
| `RestartEffect` | `effectID` | Restart a cancelled effect |
| `ChainEffect` | `fromID, toID` | Chain two effects sequentially |
| `CreateStarfield` | `numStars, radius, resetDepth, x, y, z, [reverse=false]` | Create a 3D starfield |
| `StopStarfield` | _(none)_ | Stop the starfield |
| `StartScrollEffect` | `texIdx, direction, speed, tileW, tileH, [delay=0]` | Scroll a texture layer |
| `StopScrollEffect` | `texIdx` | Stop a scroll layer |
| `PauseScroll` | `texIdx` | Pause a scroll layer |
| `ResumeScroll` | `texIdx` | Resume a paused scroll layer |
| `UpdateScrollSpeed` | `texIdx, speed` | Change scroll speed on the fly |
| `CreateParticleExplosion` | `x, y, maxParticles, maxRadius` | Spawn a 2D particle explosion |
| `StopTextScroller` | `effectID` | Stop a text scroller |
| `PauseTextScroller` | `effectID` | Pause a text scroller |
| `ResumeTextScroller` | `effectID` | Resume a text scroller |
| `SaveAndSuspendFXForScene` | _(none)_ | Save & pause FX for scene transition |
| `RestoreFXAfterScene` | _(none)_ | Restore suspended FX after scene |
| `DiscardSavedFXState` | _(none)_ | Discard saved FX state |
| `InitWarpDotTunnel` | `x, y, z, minR, maxR, spin, speed, [reverse=false], [dots=24], [density=3]` | Start warp dot tunnel |
| `StopWarpDotTunnel` | _(none)_ | Stop warp dot tunnel |

**Scroll direction values:** `ScrollRight`, `ScrollLeft`, `ScrollUp`,
`ScrollDown`, `ScrollUpLeft`, `ScrollUpRight`, `ScrollDownLeft`,
`ScrollDownRight`

**Spin cycle values:** `Clockwise`, `AntiClockwise`, `None`

---

### Camera

| Function | Arguments | Description |
|----------|-----------|-------------|
| `JumpTo` | `x, y, z, [speed=1], [focus=false]` | Animate camera to position |
| `JumpToWithYawPitch` | `x, y, z, yaw, pitch, [speed=1], [focus=false]` | Jump with orientation |
| `JumpBackHistory` | `[numJumps=1]` | Rewind camera jump history |
| `RotateX` | `degrees, [speed=1], [focus=false]` | Rotate around X axis |
| `RotateY` | `degrees, [speed=1], [focus=false]` | Rotate around Y axis |
| `RotateZ` | `degrees, [speed=1], [focus=false]` | Rotate around Z axis |
| `RotateXYZ` | `xDeg, yDeg, zDeg, [speed=1], [focus=false]` | Rotate on all axes |
| `StopRotating` | _(none)_ | Halt any ongoing rotation |
| `PauseRotation` | _(none)_ | Pause current rotation |
| `ResumeRotation` | _(none)_ | Resume paused rotation |
| `RotateToOppositeSide` | `[speed=1]` | Flip 180 degrees |
| `MoveAroundTarget` | `rotX, rotY, rotZ, [continuous=false]` | Orbit around target |
| `SetFieldOfView` | `fovDegrees` | Change camera FOV |
| `SetNearFar` | `near, far` | Set near/far clip planes |
| `CancelJump` | _(none)_ | Abort an in-progress jump |
| `ClearJumpHistory` | _(none)_ | Clear jump history stack |
| `SetRotationSpeed` | `degreesPerSecond` | Set rotation speed |

---

### SoundManager

| Function | Arguments | Description |
|----------|-----------|-------------|
| `PlayImmediateSFX` | `sfxID` | Play an SFX immediately by numeric ID |
| `SetGlobalVolume` | `volume` | Set master volume (0.0 – 1.0) |
| `SetCooldown` | `sfxID, seconds` | Set cooldown between plays |
| `ClearCooldown` | `sfxID` | Remove cooldown for an SFX |
| `StartPlaybackThread` | _(none)_ | Start the audio worker thread |
| `StopPlaybackThread` | _(none)_ | Stop the audio worker thread |

**SFX_ID values** (from `SoundManager.h`):

| Name | Value |
|------|-------|
| `SFX_CLICK` | 1 |
| `SFX_BEEP` | 2 |

---

### SceneManager

| Function | Arguments | Description |
|----------|-----------|-------------|
| `SetGotoScene` | `sceneTypeID` | Queue a scene transition |
| `InitiateScene` | _(none)_ | Execute the queued scene switch |

**SceneType numeric values:**

| Name | Value |
|------|-------|
| `SCENE_NONE` | 0 |
| `SCENE_INITIALISE` | 1 |
| `SCENE_GAMETITLE` | 2 |
| `SCENE_INTRO` | 3 |
| `SCENE_INTRO_MOVIE` | 4 |
| `SCENE_GAMEPLAY` | 5 |
| `SCENE_GAMEOVER` | 6 |
| `SCENE_CREDITS` | 7 |
| `SCENE_EDITOR` | 8 |
| `SCENE_LOAD_MP3` | 9 |

---

### GamePlayer

| Function | Arguments | Description |
|----------|-----------|-------------|
| `SetPlayerState` | `playerID, stateID` | Set player activity state |
| `StartPlayerTimer` | `playerID` | Start player event timer |
| `StopPlayerTimer` | `playerID` | Stop player event timer |
| `ResetAllPlayerStats` | _(none)_ | Reset all player statistics |

**PlayerState numeric values:**

| Name | Value |
|------|-------|
| `INACTIVE` | 0 |
| `ACTIVE` | 1 |
| `DEAD` | 2 |
| `RESPAWNING` | 3 |
| `SPECTATING` | 4 |
| `PAUSED` | 5 |
| `DISCONNECTED` | 6 |

---

### GUIManager

| Function | Arguments | Description |
|----------|-----------|-------------|
| `CreateAlertWindow` | `"message"` | Open a GUI alert window |
| `RemoveWindow` | `"name"` | Remove a named window |
| `SetWindowVisibility` | `"name", visible` | Show/hide a named window |

---

## Integration Guide

### 1. Declare the global instance (main.cpp)

```cpp
ScriptManager scriptManager;
```

### 2. Initialise after all subsystems are ready (main.cpp)

```cpp
scriptManager.Initialize(
    &fxManager,
    &threadManager,
    &soundManager,
    &guiManager,
    &scene,
    &gamePlayer,
    &physics,
    &js,
    &renderer->myCamera
);
```

### 3. Load and execute at each scene start

Call this wherever scenes are initialised (e.g. inside `SwitchToGamePlay()`
or the equivalent switch block):

```cpp
// Synchronous — blocks until script finishes
if (scriptManager.LoadSceneScript(scene.stSceneType))
    scriptManager.ExecuteScript();

// Asynchronous — returns immediately, script runs on background thread
if (scriptManager.LoadSceneScript(scene.stSceneType))
    scriptManager.ExecuteScriptAsync();
```

### 4. Call Update every frame (render loop)

```cpp
scriptManager.Update(deltaTime);
```

### 5. Honour STOP MOUSE / STOP GAMEPAD in the input loop

```cpp
if (!scriptManager.IsMouseStopped()) {
    // process mouse input
}
if (!scriptManager.IsGamepadStopped()) {
    js.processJoystickInput();
}
```

### 6. Stop script on scene exit / resize

```cpp
scriptManager.StopExecution();
```

---

## Script Loading at Scene Start

The ScriptManager resolves the script path as:

```
Scripts/<SCENE_NAME>.cgs
```

Where `<SCENE_NAME>` is the C++ enum name (e.g. `SCENE_GAMETITLE`).

If the file does not exist, `LoadSceneScript()` returns `false` silently —
no error is raised and execution continues with engine defaults.

**Scene-to-file mapping:**

| SceneType | Script file |
|-----------|-------------|
| `SCENE_INITIALISE` | `Scripts/SCENE_INITIALISE.cgs` |
| `SCENE_GAMETITLE` | `Scripts/SCENE_GAMETITLE.cgs` |
| `SCENE_INTRO` | `Scripts/SCENE_INTRO.cgs` |
| `SCENE_INTRO_MOVIE` | `Scripts/SCENE_INTRO_MOVIE.cgs` |
| `SCENE_GAMEPLAY` | `Scripts/SCENE_GAMEPLAY.cgs` |
| `SCENE_GAMEOVER` | `Scripts/SCENE_GAMEOVER.cgs` |
| `SCENE_CREDITS` | `Scripts/SCENE_CREDITS.cgs` |
| `SCENE_EDITOR` | `Scripts/SCENE_EDITOR.cgs` |
| `SCENE_LOAD_MP3` | `Scripts/SCENE_LOAD_MP3.cgs` |

---

## Error Handling

| Situation | Behaviour |
|-----------|-----------|
| Script file missing | `LoadSceneScript()` returns `false`; no error logged |
| Unknown command | Error logged; command skipped; execution continues |
| Malformed `Execute` call | Error logged; function skipped |
| Unknown `Execute` function | Error logged; function skipped |
| `ALERT CRITICAL` | Alert shown, then `QUIT` is called |
| Parse error in DETECT_COLLISION | Error logged; rule not registered |

Query errors at any time:

```cpp
if (scriptManager.HasError())
    debug.Log("Script error: " + scriptManager.GetLastError());
```
