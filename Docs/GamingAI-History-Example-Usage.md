# GamingAI history, listener and strategy exchange

All new AI policy and storage live in **GamingAI.h / GamingAI.cpp**. The existing game files supply state, collision/rule facts and rendering adapters. The recorder and listener use the existing `THREAD_AI_PROCESSING` worker. No separate AI framework or external AI service is required.

Recording starts when the updated engine runs. Editing these sources does not attach a recorder to an older running executable.

## Settings and lifecycle

The existing **Game Play** configuration tab now includes:

| Setting | Default | Behaviour |
|---|---|---|
| AI Session History | Required | Record named human and demo sessions, including idle-input observations. |
| Recall Last 3 Days at Startup | On | Show recent activity before play; Fire/Enter starts now, Backspace skips the recall screen. |
| Learn and Adapt Strategies | On | Adjust human challenge using recent sessions. Autonomous demos continue consuming core strategy when this is off. |
| 15 Second Restart Countdown | On | After game over, wait for name entry to finish, then offer a cancellable vector countdown. Fire/Enter restarts; Backspace returns to attract mode. Off preserves the existing ending flow. |
| Jumpman Level 2+ Threats | 15% | Combined enemy/trap/emitter budget from level two onward. Counts and terrain hazards apply at level generation. |

Settings persist in `GameConfig.cfg`. Startup and restart choices are journalled. Pauses, the configuration window and high-score name entry do not consume the countdown. A key used to submit a name cannot also confirm a restart without a release. Soldier On's final congratulations/fireworks hold still waits indefinitely for acknowledgement before entering the ending flow.

Scene changes, internal resets, game over, completion, explicit END aborts and application shutdown close or annotate sessions. Existing reset routines restore game state, while shared effects, score popups, banners, sounds, transient advice and encounter memory are reset at their lifecycle boundaries. Interrupted processes recover the last checkpoint as an aborted session at its last recorded activity time.

The first active gameplay observation supplies the initial mode, wave, floor, area, position, health, armour, weapon, ammunition, hostiles and carry state. These facts are preserved in the checksummed session summary and published by the worker as `initial-state.json`. `lookup.json` includes the newest verified human and demo run per game/player in `quickReview`, with starting facts and movement, firing, idle, damage and evasion statistics. Starting health normalizes damage analysis across different health pools. Human and demo learning profiles remain separate.

Demo adapters refresh core advice before the first decision after game/controller changes. They never carry another game's cached strategy into that decision. Failed request enqueue uses neutral core defaults and retries; this is not reported as an acknowledged learned decision.

`RequestGameRecovery` submits at most one bounded, value-only job to the existing AI worker; `PollGameRecovery` returns immediately while it is pending. No game pointers or world mutations cross the worker boundary. The worker evaluates up to 16 adapter-supplied legal escape candidates, prioritizes minimum danger, and journals the request/result to `recovery-state.json`. Session and terrain revisions invalidate stale replies. Soldier On supplies longer checked detours after stalled movement, keeps its local safety planner running while waiting, and rechecks live collision and attack facts before executing a returned route. A result with no safe route never teleports the player or invents a passable cell.

Exact live-scene coordinate replay remains unfinished: current high-score playback uses recorded video, and the history records player observations rather than complete per-frame state for every moving object. Reproducing a live scene requires game-specific initial-world and object-state capture/restore adapters; a navigation recovery response does not restore a replay scene.

## Files and retention

Files are generated beneath `Assets/AIHistory`:

| File | Purpose |
|---|---|
| `updates-<UTC-hour>.jsonl` | Ordered tick, lifecycle, request, command, decision, boss-defeat and board events. |
| `sessions/game-<scene>-player-<slot>-<id>.json` | Named `unique-analysis` summary, player name, outcome, activity, losses, stalls, boss defeats and board-move statistics. |
| `active.json` | Checkpoint for interrupted-session recovery. |
| `lookup.json` | Capabilities, typed API routes, human profiles and separate demo profiles. |
| `board-advice.json` | Latest chess strategy request, suggestion or committed/rejected move. |
| `acknowledgement.json` | Latest external command acceptance or execution result. |
| `listener-state.json` | Monotonic external-command sequence watermark. |

The rolling retention window is **21 × 24 hours**, using UTC rather than calendar dates. Background maintenance runs every minute, deletes expired hourly journals/summaries and trims the boundary hour. Three-day recall uses 72 hours. Strategy analysis uses up to the newest **15 eligible games per scene/player/role**. Demo practice is kept separate from human challenge profiles. Legacy GamingAI retention configuration is also capped at 21 days.

Each journal event has UTC time, process boot time, event order, request sequence, session, scene and player. Boot time plus event order identifies a retried event; consumers should deduplicate repeated writes using that pair. Tick snapshots record input/aim, action keys, focused client-pointer coordinates, score, lives, mission/mode, position and available game-specific status. Soldier On additionally supplies floor/building, elevation, health, armour, weapon, magazine and remaining enemies. These are observations and summaries, not a full world-save/replay format.

The compact `frame` array order is: dt, score, lives, wave, mode, x, y, moveX, moveY, aimX, aimY, buttons, playing, hasPosition, over, complete, floor, area, height, health, armour, weapon, ammo, hostiles, cursorX, cursorY, actions. Coordinates follow the game's adapter; pointer coordinates are normalized to the client window. The action bit order is the `actions` table in the shared frame adapter in `main.cpp`.

## Listener and command execution

Every vector game passes through `AdvanceGameLifecycle`, `RequestGameStrategy` and `RecordGameUpdate`. Cached strategy replies use an average constant-time scene/player lookup; board status uses three fixed slots. Existing dedicated rule/collision routines remain responsible for legal execution. The shared raider system, Soldier On planner and board engines consume recommendations through their adapters; other native behaviours retain their existing direct execution paths.

External commands require the following master switch in `Includes.h`:

```cpp
#define DEFINE_EXT_CMD_SEQUENCE
```

It is commented out by default. The spelling `DEFINE_EXT-CMD-SEQUENCE` cannot be a C++ macro because hyphens are operators.

`SubmitExternalGameCommand` supports typed Pause, Resume, Restart and Aggression routes. The optional `command.json` inbox accepts the same routes:

```json
{
  "scene": 36,
  "player": 1,
  "command": 3,
  "value": 0,
  "sequence": 101,
  "expiresUtcMs": 0
}
```

Replace `expiresUtcMs` with a current UTC deadline no more than 60 seconds ahead. Codes are 1 pause, 2 resume, 3 restart, 4 aggression. The sequence must increase; the player/scene must match before execution. Aggression is bounded. The worker journals and flushes an accepted command before publishing it to the engine; the engine reports whether it actually applied it. A queued acknowledgement does not mean execution has already occurred. Inbox polling and execution happen on the next worker/frame opportunities. No shell execution or raw function addresses are accepted.

## Chess, 3D Chess and Xiangqi

The existing chess search workers call `RequestBoardStrategy` with the private position hash, IQ, move number, piece count and purpose. GamingAI acknowledges a goal: development, king safety, initiative or endgame. `OrderBoardMoves` prioritizes only the legal root candidates supplied by the existing engine. Search/evaluation, deadlines, opponent personalities and variant rules remain in the existing engines.

At IQ 160 and above, the former opening depth-four cap and opening random noise are removed. Search still obeys its configured time/depth budget and the existing variant limits. The record reports the depth actually completed, including a depth-zero legal fallback when applicable.

The engine reports its suggestion through `AcknowledgeBoardSuggestion`. Immediately before play, the adapter checks both the current position hash and current legal-move list; this also covers the 3D follow-camera preview delay. `RecordBoardMove` records the actual result and whether the advice was accepted/applied. A human accepting a matching hint can be correlated with its advice request. Replays are excluded from live learning. Captures and central-development choices feed the board profiles used in subsequent matches.

The board's status strip displays the acknowledged goal, suggested move, completed depth and move acknowledgement. `GetBoardStatus` provides the same information as a value snapshot without file IO. `board-advice.json` includes requested/observed/resulting position hashes, request, IQ, goal, from/to/promotion, score, completed depth, acknowledgement, application and human/automatic origin.

## Soldier On and Jumpman behaviour

- Soldier On contains 282 normal, 1,000 tower and 1,000 underground missions: 40 themes × 25 layouts for each of the latter modes. See [mission usage](SoldierOn-Missions-Example-Usage.md).
- Normal and underground maps start at 48 × 48 cells and grow more slowly. Early tower interiors start at 32 × 32, increasing to 56 × 56.
- Boss HP and damage start at 15% of their previous wave-scaled values. The factor increases by 0.5 percentage points per wave after wave 5, reaches 100% at wave 175 and holds. Delayed boss strikes carry this factor and their originating floor.
- Defeated boss encounters are remembered by mode/mission/building/floor/kind for the current run and cannot respawn there. New games clear transient defeat memory; history retains the defeat for learning. The fixed 2,048-slot encounter table avoids per-death allocations.
- Soldier On boss fights use `GamingAI::CombatMoveDanger`, `BossCombatRange` and safety-prioritized `PlanNavigationStep` to evaluate bounded attack facts from the game adapter. Twelve directions use a 0.07–0.22-second replan interval and 0.6–1.0-second prediction horizon, adjusted by recalled avoidance/prediction values. Minimum predicted danger precedes progress near bosses or enabled lethal vehicles even when adaptation is disabled. Aim/fire remain independent of movement, and `ShouldUseCombatMedkit` permits healing while firing. Clear arenas and tower halls are generated by the game; no AI policy or learning is added to the generator.
- Light/heavy/boss tank maximum speeds remain 1.8/1.1/0.9 units per second, with a level factor rising from 35% to 100% by level 40. Other mobile ground vehicles are capped at 3 units/second. Tracked hulls turn at at most 35 degrees per second and pivot before advancing; low-health and downhill boosts cannot bypass the cap. `PlanGroundVehicle` evaluates checked movement directions and predicted player fire before bracing or advancing; game adapters supply collision and attack facts.
- Explosion damage uses shared three-dimensional radial falloff, reaching zero at the blast radius. Player/self-blast squares that factor for milder peripheral damage; enemy and sentry damage retains linear falloff. Projectile and delayed-strike ownership prevents cross-floor damage.
- Outdoor demo routes reject border crawling, allow inward recovery from a displaced position, avoid lava shores and retain recent-position memory when changing a blocked task. Final input smoothing cannot override the game bot's checked movement. Indoor corridors retain their navigation rules.
- `FindTerrainRoute` adds a cached full-map search with a strict non-canyon/bridge pass before canyon fallback. Fixed thread-local arrays bound the search to 512 × 512 cells. The game supplies cell heights, passability, canyon/bridge facts and a revision; the AI owns route policy. `DemoPlanningInterval` and `DemoPredictionHorizon` adapt reactions to recalled avoidance/prediction values. Lethal vehicle bodies are included in demo threat facts when the run-over option is active.
- Lava/land and raised-wall boundaries retain fine terrain cells even when AUTO reduces detail. Volcanic generation includes seeded lava basins and connected level land approaches.
- Supply destinations and ordinary pickups appear on the radar and full map. Cyan delivery markers distinguish queued, inbound, falling and delivered states; pickups respect their actual floor and explored terrain. The existing automatic ammunition handover remains.
- Bestiary models centre their actual projected geometry, suppress the Soldier On icon bob and use a roughly 14% larger card scale.
- Jumpman level one retains its existing settings. From level two, the default 15% budget is selected across the combined roster, pits, guns, falling-rock sources and rolling-ball source, with integer rounding and at least one threat. Chasms and ordinary gaps are reduced separately. Player movement and jumping are unchanged.

## Timing and validation

The game thread submits fixed-size records to a bounded 16,384-event queue. A 256-event retry batch survives worker IO errors. File IO, retention and learning stay on the AI worker. A full queue or storage failure raises a visible recording-fault indication; accepted external commands are not forwarded without their audit. Persistent storage failure can prevent complete recording and should be checked through the fault/log indication. Continuous tick recording also consumes disk space; the retention worker bounds its age.

Run `python Scripts/validate-gaming-ai.py`, `python Scripts/validate-soldier-missions.py` and `python Scripts/validate-vector-audio.py` for source/data contracts and independent models. They do not compile or execute the engine. Manual builds/playtesting must verify rendering, navigation, legal board play, cancellation/undo, storage recovery, FPS and supported platform toolchains. The new core uses C++17 facilities and existing platform abstractions; platform guards are not proof of completed OS ports.


## Demo stall watchdog, board deadlines and replay context (2026-11-14, uncompiled)

**Stall watchdog.** `GamingAI::ObserveDemoStall(AIDemoWatchdog&, dt, playing, hasPosition, x, y, score, lives, wave, dirX, dirY)` decides when an autonomous demo is stuck. Progress is a score / lives / wave change or the position moving more than 0.75 units; games that report no position (board games) are never flagged. After 4 s without progress it answers `Refresh` (the adapter drops cached strategy via `RefreshGameStrategy(..., true)`); after 8 s, and every 6 s after that, `Probe`: a unit direction held for 0.8 s, stepping round a 16-point circle with stride 5 so successive pulses take different headings. `DemoBot` in main.cpp only supplies the previous completed frame (`g_aiLastFrame`) and overlays the pulse on the stick plus fire; it skips the overlay while a contact separation is steering and on the grid-direction Pac-Man games. Normal bot decisions and the games' own collision rules are untouched.

**Board search deadline.** `GamingAI::BoardSearchDeadline(limit, demo, unlimitedSeconds)` replaces the hard-coded safety nets in Chess / 3D Chess / Xiangqi: a configured limit still gets limit + 3 s, and a demo with an unlimited search now falls back to a legal move after 8 s at most instead of 12 - 14 s.

**Replay context.** `AIReplayContext` (mode, view, weather, yaw, pitch, distance, time of day, seed, music file name and time index) rides in `AIGameFrame::replay`. `CaptureReplayContext` (main.cpp) fills it every frame (music is polled once a second): Soldier On supplies mode, camera mode, weather, time of day and starting mission; Racing its camera / view modes and chase yaw; Chess its view, yaw and pitch; 3D Chess / Xiangqi view flags, yaw and pitch; every game supplies the music. The worker journals a `replay` object in an Update row only when the context changes (camera drift below 5 degrees and music time below 10 s is ignored), stores the newest copy per scene and source in `replay-context.json`, keeps it in the session summary as `replayContext`, and reloads the file at start-up. `GetReplayContext(scene, demo, out)` returns the live copy first, then the persisted one.

`RestoreReplayContext` runs when a demo starts or restarts: the same music file seeks to its time index; a different one is requested from the playlist thread through the new `MediaPlayer::requestTrack(file, ms)` (the switch and the seek run on the player's own thread, and a failed load falls back to the next playlist entry); Soldier On's next run starts in the recorded mode. Camera and world fields are deliberately not forced onto a running demo because the demo director (`DemoViewMode`) owns them; other consumers can read them from `GetReplayContext`. Recorded MP4 playback still shows the captured video; it is not re-rendered from this context.


See [Arcade setup, score/time records and Player Play-back](Arcade-Playback-Example-Usage.md) for the current shared controls, recording retention, native validation and rendering changes.
