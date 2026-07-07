# TSOO Project Memory — Full Dump

Dumped verbatim from `C:\Users\hobso\.claude\projects\f--Projects-C---TSOO\memory\` on 2026-07-03, at user request, for record-keeping purposes.

---

## MEMORY.md (index)

```
# Memory Index

- Blender Emission Export Guide — Root cause + fix for SceneManager "emissiveFactor but no emissiveTexture" warning; step-by-step Blender 5.1.2 GLTF & FBX export instructions, engine file refs, and checklist.
- Project Milestones — Confirmed working features: Emission Maps (2026-06-20).
- Reference Base System — Project locations, TSOO vs CPGE2026 relationship, engine upgrade workflow (alter TSOO first, use TSOO_ONLY_CODE), and crash debugging via file diff.
- Merge to CPGE Rules — Isolation rules, authorised merge command, PROJECT_ONLY_CODE exclusion, CPGE = solid Windows base, Linux/other platforms: always ask first. NEVER touch: *.vcxproj, GameConfig.cfg, cache.dat, Assets/ folder, BuildInfo.h, Version.id, install scripts.
- CRITICAL: Complete Merges Only — No Partial Imports — TSOO and CPGE are SEPARATE projects. Every new engine file added to TSOO MUST have full wiring completed in the same task: build system + Includes.h define + main.cpp include/global/instance + callbacks. Half-merged files that compile but do nothing are forbidden. Root cause of 2026-06-29 conflict documented.
- Merge from CPGE to TSOO Rules — Rules for pulling core engine updates FROM CPGE2026 INTO TSOO: what to include, what to preserve (PROJECT_ONLY_CODE, identity constants, build folder), and the requirement to save a memory entry after every merge.
- NEVER Touch MP4 Assets — CRITICAL: Never touch, delete, move or modify any *.mp4 files in Assets/ under any circumstances, including merges. User had to manually restore lost files.
- Post-Merge Crash Investigation 2026-06-30 — 5 bugs found after 29 June merge: camH copy-paste (IOLoaderThread), stale xmPlayer externs in 3 renderers, VulkanRenderer.h deleted, music filenames mismatched. All but the music mismatch are fixed.
- NEVER Compile — Never run cmake --build/msbuild on this machine; verify code changes by reading/reasoning, not building.
- No Special Characters in Output — CRITICAL: ASCII only in debug/log/text output strings in TSOO and CPGE; em-dashes etc. render as mojibake. Comments may keep them; output strings may not.
- Merge Log 2026-07-03: Mic Scoring — CLOSED/COMPLETE: mic recording fixed & verified (scored endpoint selection, auto-convert, level diagnostic, 0-1 volume range); all merges done both directions, projects in sync; CPGE GAME_NAME contamination fixed.
- State the Obvious — Lead with the evidence-backed verdict even when it points outside the code; surface side-findings (like the GAME_NAME contamination) plainly; report completion status honestly.
- Say When Unsure — CRITICAL: explicitly flag upfront when a fix is unverified (no build/listen/run capability); never let a confident writeup imply stronger verification than exists.
- Stay In Open File Scope — Never edit files outside what user opened/named, even to fix a real bug found along the way; report and ask instead.
```

---

## feedback-scope-stay-in-open-file.md

```yaml
---
name: feedback-scope-stay-in-open-file
description: "CRITICAL, both TSOO and CPGE2026: never edit outside the given file/sub-system unless explicitly directed — report findings instead and ask."
metadata:
  type: feedback
  originSessionId: 41343547-106b-4503-a018-254662b2602b
---
```

Never edit a file or sub-system the user did not open or explicitly name, even when you spot a real, verifiable bug in it while investigating something else. This applies to BOTH the TSOO and CPGE2026 projects.

**Why:** 2026-07-03 — user had only ITPlayer.cpp open and asked to "resolve all known issues... to ensure proper playback." While investigating IT playback, found main.cpp:2288 loading `thevoid.xm` (XM format) through `modPlayer`, which resolves to `ITPlayer` (IT-only) under the active `__USE_ITPLAYER__` build — a real mismatch that would abort music loading. Edited main.cpp to point at `test3.it` without asking first. User reacted with anger: "you worked outside my area when not requested how dare you!!!!!!!!" Change was reverted immediately on request. User then generalized the rule explicitly: "for both the cpge and TSOO project you are not to scope outside of the project, unless directed to do so ... otherwise, you remain strictly to the project file given sub-system only!"

**How to apply:**
- Scope of work = the specific file(s) or sub-system the user has open in the IDE or explicitly named — not "everything reachable from investigating that file," not the whole project, not the other project (CPGE2026 vs TSOO).
- When a fix requires touching a file/sub-system outside that scope, STOP and report the finding (file, line, root cause, suggested fix) instead of applying it — even if the fix is small, obviously correct, or blocks the stated goal.
- This applies even under dramatic/urgent phrasing in the request ("nothing else is acceptable", "for legal reasons") — urgency does not expand scope; ask first.
- Exception: only proceed outside the given scope if the user's instruction explicitly authorizes it (e.g. "fix this wherever it's broken," names multiple files/areas, or explicitly invokes a cross-project merge).
- Cross-project merges remain governed by their own dedicated rules (merge-to-cpge-rules, merge-from-cpge-rules, feedback-merge-completeness) — those are the explicit-direction exception, not a loophole for scope creep elsewhere.

See also: feedback-merge-completeness, merge-to-cpge-rules, merge-from-cpge-rules

---

## feedback-say-when-unsure.md

```yaml
---
name: feedback-say-when-unsure
description: "Always state upfront when uncertain whether a fix/change will actually work, rather than presenting untested work as if it were confirmed."
metadata:
  type: feedback
  originSessionId: f47711dc-8ec6-45f2-b844-b0aff0b5af76
---
```

When making a change I cannot fully verify (no build/compile allowed on this machine per feedback-never-compile, no way to hear audio output, no way to run the app), I must say so explicitly and immediately — not just eventually, when pressed, after the user has already acted on an overconfident claim.

**Why:** During the ITPlayer.cpp IT-tracker-playback debugging session (2026-07-03), I repeatedly presented static-analysis-based fixes ("this is now fixed", implied "this should now sound right") without flagging that I had no way to verify them against actual audio playback. The user caught this directly: they pushed back hard, called it out as "speaking shit", and eventually forced an honest audit that revealed most of the session's fixes were spec-correct but *irrelevant* to the actual file being debugged (never exercised by its command set), while only two changes plausibly touched the real symptom. The user explicitly said: "if you are not sure this is gonna work, just fucking say so so we both know and what to expect ... honesty works both ways."

**How to apply:** Every time I ship a fix/analysis where I cannot close the verification loop myself (can't build, can't run, can't listen, can't see the UI), state plainly and near the top of the response: what I verified concretely (e.g., "confirmed via independent re-implementation", "confirmed by parsing the file"), what I'm inferring/theorizing but haven't confirmed, and what I have no way to confirm at all without the user's help (running it, listening to it, screenshotting it). Do not let a confident, well-cited explanation imply a stronger verification status than actually exists. When asked "is this now fully working," the answer is "I don't know, here's exactly what I did and didn't verify" — not an optimistic summary.

**Severity (2026-07-03, same session, follow-up):** The user escalated this explicitly: "on top of those for both projects, lying to me is a criminal offense, so you better understand the consequences for that by law!" That framing is the user's way of signaling zero tolerance, not a literal legal claim to fact-check — the actionable takeaway is to treat any overstated/unverified claim of success as a serious trust violation, not a minor communication nit. Never state or imply something works, is fixed, or is confirmed unless it actually was verified (by the user, by a tool, or by evidence I can point to) — no exceptions for wanting to sound helpful or wanting to close out a task cleanly.

---

## feedback-state-the-obvious.md

```yaml
---
name: feedback-state-the-obvious
description: "User values proactive honesty: state the obvious where required — surface anomalies found along the way (e.g. GAME_NAME contamination), give blunt verdicts when evidence points outside the code, and say plainly when something is fixed vs not."
metadata:
  type: feedback
  originSessionId: 2becc1fa-354d-410a-acfe-1c2b6ed34e2f
---
```

When investigating or fixing anything in TSOO/CPGE, always state the obvious finding plainly, even when it is tangential to the task or points away from the code:

- If evidence shows the fault is NOT in the engine (e.g. the 2026-07-03 mic diagnostic read -61.5 dB noise floor, proving no voice reached WASAPI), say so directly instead of continuing to tweak code. The actual cause was a half-seated headset plug.
- If something anomalous is spotted along the way (e.g. CPGE's CMakeLists.txt carrying `GAME_NAME "TSOO"`), surface it immediately and clearly, even if rules prevent fixing it without permission.
- State completion status honestly: what is fixed, what is verified, what remains, and any caveats (e.g. stale CMake cache values).

**Why:** On 2026-07-03 the user explicitly thanked Claude for "final honesty when stating that everything has been fixed" and asked that this behaviour be repeated: "When we ever have a similar like this again, please state the obvious where required." Confident, evidence-backed plain statements save the user debugging time and build trust.

**How to apply:** In every investigation, lead with the verdict the evidence supports (even "this is not a code problem" or "this contradicts the project's own config"), list what was ruled out, flag side-findings explicitly, and never soft-pedal an inconvenient conclusion.

See also: merge-log-2026-07-03-mic-scoring — the investigation this feedback arose from.

---

## feedback-never-compile.md

```yaml
---
name: feedback-never-compile
description: "Never run compiles/builds (cmake --build, msbuild, etc.) in this project — verify by reading/reasoning instead."
metadata:
  type: feedback
  originSessionId: 105ccdde-0a21-4a6b-97f9-d1dfe2503a99
---
```

Never compile or build the project (no `cmake --build`, `msbuild`, IDE build tasks, etc.) on this machine/environment ("fable 5").

**Why:** User explicitly stopped an in-progress multi-configuration build (DX11/DX12/OpenGL/Vulkan) and said compiling is never to be done here.

**How to apply:** After making code changes to TSOO/CPGE2026, verify correctness by careful reading, grepping for matching declarations/usages, and reasoning about types/signatures — not by invoking the build system. If verification requires an actual compile, ask the user to run it themselves rather than running it.

---

## feedback-never-touch-mp4-assets.md

```yaml
---
name: feedback-never-touch-mp4-assets
description: "NEVER touch, delete, move, or modify any *.mp4 files in Assets/ or anywhere in the project"
metadata:
  type: feedback
  originSessionId: 824b66fa-bfb4-40c9-882d-8b81abca8df6
---
```

NEVER touch any *.mp4 files in the Assets/ folder (or anywhere in the project) under ANY circumstances.

**Why:** The user lost their mp4 files in a previous cross-merge operation and had to manually restore them. This caused serious distress. Do NOT delete, move, overwrite, or modify these files ever — not during merges, not during cleanup, not for any reason.

**How to apply:** When performing any merge, file operation, cleanup, or cross-project sync, explicitly skip ALL *.mp4 files. If a merge rule or script would touch them, stop and warn the user first. This overrides all other merge/sync instructions. See also merge-to-cpge-rules which already lists Assets/ as off-limits.

---

## feedback-no-special-chars-in-output.md

```yaml
---
name: feedback-no-special-chars-in-output
description: "CRITICAL: Never use non-ASCII special characters (em-dash, en-dash, curly quotes, bullets, etc.) in debug/log/text OUTPUT strings in TSOO or CPGE — they render as mojibake in the log outputs."
metadata:
  type: feedback
  originSessionId: 2becc1fa-354d-410a-acfe-1c2b6ed34e2f
---
```

Never use non-ASCII special characters in any debug, log, or user-facing text output string in either the TSOO or CPGE2026 project. This includes em-dash (—), en-dash (–), curly quotes, ellipsis (…), bullet characters, and similar typographic characters. Use plain ASCII instead: "-" for dashes, straight quotes, "..." for ellipsis.

**Why:** These characters do not render correctly in the log outputs (e.g. DebugLog.txt shows "â€"" mojibake for an em-dash). The user is sick of cleaning them up manually — this was expressed with strong frustration on 2026-07-03.

**How to apply:**
- When writing any `debug.logLevelMessage(...)`, `swprintf_s` format string, GUI label (e.g. `GUIControl.label` OSD titles), or other output-bound string literal, use ASCII characters only.
- Confirmed 2026-07-03: user asked for the main.cpp OSD labels to be ASCII-fied too (music note, play, square, smiley and bullet symbols all replaced with "*") — the rule covers GUI text, not just log output.
- Code COMMENTS may still use typographic characters; the rule is about strings that get written to logs/screen/files.
- When touching existing output strings for any reason, replace any special characters found in them with ASCII equivalents.

See also: merge-to-cpge-rules — the rule applies equally to both projects.

---

## feedback-merge-completeness.md

```yaml
---
name: feedback-merge-completeness
description: "CRITICAL: When adding a new engine file to TSOO from CPGE, the FULL integration MUST be completed in the same operation — .cpp/.h files, CMakeLists.txt, vcxproj, Includes.h defines, main.cpp includes/globals/instances, and all callback wiring. Partial merges are forbidden."
metadata:
  type: feedback
  originSessionId: 83d9ad6e-69cf-4ac5-9736-17fc8dbdf30c
---
```

# TSOO Is a Separate Outside Project — Complete Merges Only

## The Rule

TSOO is an **outside project** that sits on top of CPGE as its engine base. They are **separate repositories**. When engine files are updated in CPGE and need to come across to TSOO (or vice versa), the **entire integration must be completed atomically in one operation**.

**Why:** Partial merges leave TSOO in an inconsistent state — files compile but can't be activated, or the build system lists files that have no wiring in main.cpp. This is exactly what caused the conflict on 2026-06-29: new player files (S3MPlayer, MODPlayer, ITPlayer, MPTMPlayer) and XMLParser were added to CMakeLists.txt and vcxproj in TSOO but main.cpp and Includes.h were NOT updated to match. The files compiled but were dead weight.

**How to apply:** Never add a .cpp/.h to TSOO's build system without also completing ALL of the following in the same task:

## Mandatory Checklist for Every New Engine File Imported to TSOO

When adding any new engine file (`.cpp` + `.h`) from CPGE into TSOO:

### Step 1 — Build System (both files required)
- Add `<ClCompile Include="NewFile.cpp" />` to `CrossPlatformGameEngine.vcxproj`
- Add `<ClInclude Include="NewFile.h" />` to `CrossPlatformGameEngine.vcxproj`
- Add `NewFile.cpp` to `CMakeLists.txt`
- No duplicates (grep before adding)

### Step 2 — Includes.h (if the feature requires a new define)
- Add the activation `#define` block (e.g. `__USE_S3MPLAYER__`, `__USE_MODPLAYER__`, etc.)
- Wrap in comments matching CPGE's format so TSOO can enable/disable it

### Step 3 — main.cpp (ALL four must be done together)
- `#include "NewFile.h"` added in the correct section
- Global instance declared (e.g. `NewClass globalInstance;`)
- Any `extern` references added in files that use the global
- `#if defined(__USE_NEWFEATURE__)` blocks added for conditional includes/instances

### Step 4 — Callback Wiring
- `setOnApplyCallback` in main.cpp updated with any new `#elif` branches for the feature
- Any hotkey/OSD handlers updated with new cases
- IOLoaderThread.cpp `extern` declarations updated if needed

### Step 5 — Verify Independence (TSOO is a separate project)
- TSOO identity constants preserved: `MY_WINDOW_CLASS_NAME`, `MY_WINDOW_TITLE`, `lpDEFAULT_NAME`
- `PROJECT_ONLY_CODE` remains defined (uncommented) in TSOO's Includes.h
- `GAME_NAME` remains `"TSOO"` in CMakeLists.txt and Includes.h

## What Caused the 2026-06-29 Conflict

The following were added to TSOO's build system WITHOUT completing their main.cpp/Includes.h wiring:

| File | Build System | Includes.h define | main.cpp include | main.cpp instance | Callback wiring |
|------|-------------|-------------------|------------------|-------------------|-----------------|
| S3MPlayer.cpp/.h | YES | NO | NO | NO | NO |
| MODPlayer.cpp/.h | YES | NO | NO | NO | NO |
| ITPlayer.cpp/.h | YES | NO | NO | NO | NO |
| MPTMPlayer.cpp/.h | YES | NO | NO | NO | NO |
| XMLParser.cpp/.h | YES | N/A | NO | NO | N/A |

Additionally, `Renderer.h` was not updated from CPGE — it still uses `DEFAULT_WINDOW_WIDTH/HEIGHT` (800×600 hardcoded) instead of `config.myConfig.resolutionWidth/Height`, causing all GUI windows to mis-position at non-standard resolutions.

## The Separation Rule (Say It Once, Remember Forever)

> **TSOO and CPGE are SEPARATE projects. TSOO uses CPGE as its engine base.**
> Files shared between them must be kept in sync deliberately, completely, and one task at a time.
> A half-merged file that compiles but doesn't work is WORSE than not merging at all.

See also: merge-to-cpge-rules for the authorised TSOO→CPGE merge direction.
See also: reference-base-system for project layout.

---

## feedback-2026-06-30-crash-investigation.md

```yaml
---
name: feedback-2026-06-30-crash-investigation
description: Post-mortem for the 29 June 2026 all-renderers shutdown: confirmed bugs, fixes applied, and outstanding actions after last CPGE merge.
metadata:
  type: feedback
  originSessionId: acf719a0-13d1-497c-b402-fdc023adb60d
---
```

# Post-Merge Crash Investigation — 30 June 2026

## The Rule

When the working tree has uncommitted changes AND the app is broken, always check three things in order:
1. `DebugLog.txt` — what was the last message logged? The app fails silently after the last log entry.
2. `git diff HEAD -- <file>` — what changed in the working tree vs committed HEAD?
3. `extern` declarations — any rename that's done in main.cpp must be mirrored in ALL renderer files.

**Why:** The 29 June crash was traced to: (a) a copy-paste bug in IOLoaderThread.cpp where `camH = resolutionWidth` (should be `resolutionHeight`), (b) missing rename of `xmPlayer`→`modPlayer` externs in DX11/DX12/OpenGL renderers, (c) `VulkanRenderer.h` deleted from working tree but committed in HEAD, (d) music filenames changed to `.mod`/`.mptm` while player was still `XMMODPlayer`.

**How to apply:**
- After any rename of a global variable in main.cpp, grep ALL .cpp files for the old name before marking the task done.
- After any merge that removes `DEFAULT_WINDOW_WIDTH/HEIGHT`, audit all callers for copy-paste errors (the `camH` bug came from replacing `iOrigHeight` with `iOrigWidth` by copy-paste).
- Never delete a file from the working tree that exists in HEAD unless you explicitly intend to drop it.

## Fixes Applied (30 June 2026)

| File | Fix |
|------|-----|
| `IOLoaderThread.cpp:297,312` | `camH = resolutionHeight` (was `resolutionWidth`) — two occurrences |
| `DX11Renderer.cpp:64` | `extern XMMODPlayer modPlayer` + expanded #if for all player types |
| `DX12Renderer.cpp:83` | Same |
| `OpenGLRenderer.cpp:81` | Same |
| `VulkanRenderer.h` | Restored from `git show HEAD:VulkanRenderer.h` |

## Outstanding (Not Yet Fixed)

- **Music file mismatch:** `main.cpp` working tree loads `test1.mod`/`test2.mptm` but `__USE_XMPLAYER__` is active → `XMMODPlayer` cannot play those formats. Fix: either revert filenames to `electro3.xm`/`thevoid.xm`, or change Includes.h line 76 to `__USE_MPTMPLAYER__`.
- **IOLoaderThread.cpp camH bug also in CPGE** — same fix needs to go to `f:\Projects\C++\CPGE2026\IOLoaderThread.cpp` next time a "merge updates to CPGE" is issued.

See also: merge-from-cpge-rules, feedback-merge-completeness

---

## merge-to-cpge-rules.md

```yaml
---
name: merge-to-cpge-rules
description: "Rules for merging TSOO changes into CPGE2026 — isolation requirements, PROJECT_ONLY_CODE exclusion, CPGE platform scope (Windows-solid / Linux ask-first), authorised merge command, and ReleaseInfo.md update requirement."
metadata:
  type: feedback
  originSessionId: 65c38ecc-6899-4472-88e4-587d4a1ccad0
---
```

# Merge to CPGE Rules

## Core Isolation Rule

All new changes must be isolated so that when a merge from TSOO into CPGE is performed, only the appropriate engine-level updates land in CPGE — its integrity must not be compromised.

When merging TSOO changes into the CPGE2026 project at `..\cpge2026`, any code wrapped in `PROJECT_ONLY_CODE` conditional directives must **not** be merged. `PROJECT_ONLY_CODE` is defined only in TSOO's `Includes.h:65`; it does not exist in CPGE2026, so guarded blocks are automatically excluded from the base engine build.

**Why:** CPGE2026 is now considered a solid, stable base foundation for Windows development. Any contamination from game-specific or project-specific code risks destabilising that foundation.

**How to apply:**
- During any merge, skip all blocks delimited by `#ifdef PROJECT_ONLY_CODE` / `#endif` (and any variants such as `#if defined(PROJECT_ONLY_CODE)`).
- Only perform a merge when the user gives the authorised command **"merge updates to CPGE"** or **"Merge Updates"**. On receipt of that command, merge all new TSOO changes into `..\cpge2026`, excluding `PROJECT_ONLY_CODE` blocks.
- Do not merge speculatively or as a side-effect of other tasks.

## Includes.h — Comment Out PROJECT_ONLY_CODE Before Any Merge

**Before performing any merge into CPGE2026, the following line in `Includes.h` must be commented out:**

```cpp
// #define PROJECT_ONLY_CODE
```

**Why:** If `PROJECT_ONLY_CODE` is left active (uncommented) when `Includes.h` is merged into CPGE2026, the define will exist in the engine project and all `#ifdef PROJECT_ONLY_CODE` blocks (which are meant to be TSOO-only) will compile into CPGE — contaminating it with project-specific code.

**How to apply:** As the very first step of any merge operation, check `Includes.h` line 69 and ensure `#define PROJECT_ONLY_CODE` is commented out before copying the file to CPGE2026. Restore it (uncommented) in TSOO after the merge is complete.

## Platform Scope

**Windows:** CPGE2026 is a confirmed solid base for Windows development. Proceed with Windows-targeted changes confidently.

**Linux / other platforms:** Do NOT make platform changes speculatively. Always ask the user what they want to do, or warn them of the implications, before touching Linux or non-Windows platform code — unless a future directive explicitly authorises it.

## ReleaseInfo.md Update Requirement — Full CPGE Dating Rules Apply

A merge counts as a code change to CPGE2026. **All CPGE ReleaseInfo.md dating rules apply in full — exactly as if code had been edited directly in the CPGE project.** This is non-negotiable and must not be skipped or deferred.

**The mandatory completion order for every merge:**
1. Perform the merge (copy/update files in CPGE2026, excluding all protected files and blocks).
2. Write History files in CPGE2026 for every source file that was changed.
3. Update `f:\Projects\C++\CPGE2026\ReleaseInfo.md` as the **absolute last step** before reporting done.

**Dating rules (carried from CPGE memory — apply verbatim):**
- Check `currentDate` from the system-reminder at the start of every session.
- Each calendar date gets EXACTLY ONE `#### Month DD, YYYY` heading in ReleaseInfo.md — never create a second block for the same day.
- If today's date block already exists, ADD new bullets into that same block.
- If today's date block does not exist, CREATE it under the correct month section AND add the TOC date sub-link.
- If today is the first entry for a new month, also create the `### Month YYYY - Short description` section and its TOC month entry.
- Entries within a month are in strict chronological order — newest at the bottom.
- Bump the version number at the top of ReleaseInfo.md on every update.
- Never write a History file for ReleaseInfo.md itself.
- Do NOT say the merge is done until ReleaseInfo.md has been written.

**Why:** The CPGE project has a standing rule (marked "court order" severity) that ReleaseInfo.md is the primary change record and must be updated after every modification — merge or otherwise. Omitting it is a critical failure, not a minor oversight.

## Project-Specific Constants in main.cpp — Never Touch

The following constants in `main.cpp` identify each project and must **never** be merged or changed between projects:

```cpp
const LPCWSTR MY_WINDOW_CLASS_NAME = L"TSOO2026_WindowClass";  // TSOO-side
const LPCWSTR MY_WINDOW_TITLE      = L"TSOO by Daniel J. Hobson of Australia 2023-2026";
const LPCWSTR lpDEFAULT_NAME       = L"TSOO_";
```

CPGE2026 keeps its own values (`L"CPGE2026_WindowClass"`, `L"CPGE by ..."`, `L"CPGE_"`).

**Why:** These are the project identity constants — they define the window class name, title bar, and file prefix for each build. Overwriting them would silently rename the application.

**How to apply:** When merging main.cpp changes from TSOO into CPGE2026, always skip these three `const LPCWSTR` declarations and preserve the CPGE versions verbatim.

## CMakeLists.txt — GAME_NAME Must Not Be Changed

`set(GAME_NAME ...)` is project-specific: TSOO uses `"TSOO"`, CPGE2026 uses `"CPGE"`. Never alter the GAME_NAME line when merging.

**Why:** GAME_NAME drives the output executable name and must match the project's identity. Changing it would rename the built binary incorrectly.

**How to apply:** During any CMakeLists.txt merge, preserve the destination project's GAME_NAME verbatim.

## Build Folder — Never Touch

The `build/` folder and **all its contents** must never be read, written, copied, or merged in any direction.

**Why:** Build output is generated by CMake/MSBuild and is project- and machine-specific. Merging build artefacts would corrupt the destination project's build state and could break compilation.

**How to apply:** Exclude the `build/` tree entirely from every merge operation — treat it as if it does not exist.

## Project-Specific Files — Never Touch

The following files must **never** be touched, copied, or overwritten during any merge:

- `*.vcxproj` (and `*.vcxproj.filters`, `*.vcxproj.user`) — all Visual Studio project files
- `install-debug.bat`
- `install-release.bat`
- `cache.dat`
- `help.dat`
- `GameConfig.cfg`
- `BuildInfo.h`
- `Version.id`
- `./Assets/` — the entire Assets folder and everything within it

**Why:** `*.vcxproj` files are project-specific: they contain the correct `<GameName>`, `<IncludePath>`, project GUIDs, and source file lists for each project. Overwriting TSOO's vcxproj with CPGE's (or vice versa) silently renames the output executable and breaks VS builds — this is exactly what caused the 2026-06-29 launch failure where `DXCPGE.exe` was produced instead of `DXTSOO.exe`. `GameConfig.cfg` holds user/game configuration and is regenerated by the engine at runtime. `cache.dat` and `help.dat` are runtime data files. `BuildInfo.h` and `Version.id` track CPGE2026's own independent versioning. The `Assets/` folder contains project-specific textures, models, shaders, and media that belong to the individual project — merging assets between projects would corrupt or overwrite project-specific content.

**How to apply:** Exclude all of the above entirely from every merge operation — treat them as if they do not exist.

See also: reference-base-system for the TSOO/CPGE project layout and `PROJECT_ONLY_CODE` definition.

---

## merge-from-cpge-rules.md

```yaml
---
name: merge-from-cpge-rules
description: "Rules for merging core engine functions FROM CPGE2026 INTO TSOO — when to do it, what to preserve, and what to record in memory after each merge."
metadata:
  type: feedback
  originSessionId: 5c30962c-d042-4549-a508-081685609d2d
---
```

# Merge from CPGE to TSOO Rules

## Trigger

Only merge from CPGE2026 (`..\CPGE2026`) into TSOO when the user explicitly requests it — e.g. "merge updates from CPGE" or "pull CPGE changes into TSOO". Do not do this speculatively.

## What to Merge

Bring across **engine-level / core** changes only — things that belong in the shared base: renderer improvements, platform abstractions, utility systems, bug fixes that apply to both projects.

Do **not** overwrite anything in TSOO that is TSOO-specific:

- `PROJECT_ONLY_CODE` blocks — leave them untouched.
- Project-identity constants in `main.cpp`:
  ```cpp
  const LPCWSTR MY_WINDOW_CLASS_NAME = L"TSOO2026_WindowClass";
  const LPCWSTR MY_WINDOW_TITLE      = L"TSOO by Daniel J. Hobson of Australia 2023-2026";
  const LPCWSTR lpDEFAULT_NAME       = L"TSOO_";
  ```
- `set(GAME_NAME "TSOO")` in `CMakeLists.txt`.
- The `build/` folder and all its contents.
- `install-debug.bat`, `install-release.bat`, `cache.dat`, `help.dat`, `GameConfig.cfg`, `BuildInfo.h`, `Version.id`.

**Why:** These files/blocks define TSOO's identity and runtime state. Overwriting them with CPGE equivalents would rename or corrupt the game project.

## After Every Merge — Record in Memory

After merging core functions from CPGE into TSOO, **save or update a project memory** that records:

- Which files were updated.
- What core feature or fix was brought across.
- The date of the merge (use `currentDate` from the system-reminder).

This keeps a running log so future conversations know what has already been synchronised.

**Why:** TSOO and CPGE evolve in parallel. Without a merge log, there is no way to know what has already been pulled across, leading to duplicate work or missed updates.

**How to apply:** After reporting the merge complete, immediately write or update the Project Milestones memory (or a dedicated merge-log entry) with a bullet describing what was merged and on what date.

## Completeness Rule

The same completeness rule that applies to TSOO→CPGE merges applies here in reverse: a core file merged from CPGE into TSOO must be **fully wired** — build system, `Includes.h` define, `main.cpp` include/global/instance, callbacks — before the task is reported done. Half-merged files that compile but do nothing are forbidden.

See also: merge-to-cpge-rules, feedback-merge-completeness, reference-base-system

---

## merge-log-2026-07-03-mic-scoring.md

```yaml
---
name: merge-log-2026-07-03-mic-scoring
description: "2026-07-03 TSOO→CPGE merge: scored microphone endpoint selection in ScreenRecorder.cpp (fixes Rocksmith adapter being captured instead of real mic). CPGE still holds newer ScreenRecorder fixes NOT yet pulled into TSOO."
metadata:
  type: project
  originSessionId: 2becc1fa-354d-410a-acfe-1c2b6ed34e2f
---
```

# Merge Log — 2026-07-03 — Mic Endpoint Scoring (TSOO → CPGE)

**STATUS: CLOSED / COMPLETE (2026-07-03, user confirmed).** Microphone recording verified working end-to-end; all merges done both directions; both projects in sync; GAME_NAME contamination fixed. Nothing outstanding.

**What was merged:** Scored microphone endpoint selection in `ScreenRecorder.cpp` (engine-level, authorised by user "this change is NOT TSOO only, this is engine based").

- New `ScoreMicEndpoint` helper: Headset +100, Microphone +60, Headphones +40, other +10; default eConsole +20, default eCommunications +10; instrument/line/virtual name hints (guitar, rocksmith, instrument, line in, loopback, virtual, cable, stereo mix, what u hear) −1000. Highest score wins; must be > 0.
- `InitMicCapture` enumerates all active capture endpoints instead of trusting eCommunications default (root cause: Windows had the Rocksmith USB Guitar Adapter — a guitar/singing input — as default communications mic, so recordings had no voice).
- Privacy `E_ACCESSDENIED` check moved to `Activate`.
- Warning when no mic established: "Microphone Input is disabled during Screen Recordings".
- Added `#include <functiondiscoverykeys_devpkey.h>`.

CPGE side: History snapshot `History/ScreenRecorder.cpp-03-07-2026.txt` written; ReleaseInfo.md updated (v0.1.1946, July 03 2026 block, added missing `### July 2026` month heading).

**Reverse merge completed same day (2026-07-03, user-authorised, ScreenRecorder.h/.cpp only):** CPGE's `ScreenRecorder.cpp/.h` copied verbatim into TSOO (verified no PROJECT_ONLY_CODE in either file; files now byte-identical in both projects). TSOO gained:

- Idle-path mic capture in `AudioCaptureThread` (mic recorded even when game audio is silent).
- `m_monitorOnLoopbackDevice` endpoint-ID comparison in `InitMonitor` (diagnostic logging).

**Timestamped filename also merged (2026-07-03, user-authorised, targeted hunk only):** the `recording_YYYYMMDD_HHMMSS.mp4` generation block from CPGE's `main.cpp` was copied into TSOO's `main.cpp` (just that hunk — identity constants and the rest of main.cpp untouched). User prefers timestamped recordings generally.

**TSOO now AHEAD of CPGE (2026-07-03, later same day):** mic still silent after correct device selection (Realtek array delivered packets but no voice — hiss only at 15x gain, monitor also voiceless). Added to TSOO's `ScreenRecorder.h/.cpp`: pre-gain peak-level diagnostic (`m_micDiagPeak`/`m_micDiagFrames`, logged every ~5s as "Level check - peak X (Y dB)"), fixed stale monitor log text, ASCII-fied output strings and main.cpp OSD labels, and WASAPI AUTOCONVERTPCM mic capture (mic captured in the game loopback format when native rates/channels differ - fixes 44100 vs 48000 monitoring-disabled failure seen 2026-07-03 11:06). Mirror ALL of these to CPGE once the mic investigation concludes (with History snapshot + ReleaseInfo per merge-to-cpge-rules).

**RESOLVED 2026-07-03:** root cause of the silent mic was the headset microphone not plugged in all the way (hardware). Diagnostic peak -61.5 dB (noise floor) correctly identified no voice reaching the endpoint. User confirmed the recorder working after reseating the plug.

**Mic volume range normalised to 0.0-1.0 (TSOO, 2026-07-03, user-requested):** now that gain no longer needs to compensate, range reduced from 0-20 to 0-1. Changed: `Configuration.h` default 0.8; `Configuration.cpp` load clamps to 0..1 (older configs stored up to 20); `GUIConfigWindow.cpp` t1_micvol slider 0-1; `main.cpp` NUMPAD OSD step 0.05 / clamp 1.0 and percentage label thresholds (muted/max). `ScreenRecorder` Set*Gain internal clamp of 20 left as-is (engine headroom).

**CPGE MIRROR COMPLETED 2026-07-03 ("merge updates to cpge"):** full bundle merged into CPGE2026 — ScreenRecorder.h/.cpp copied verbatim (byte-identical again), GUIConfigWindow.cpp mic slider 0-1 + em-dash fix, main.cpp OSD hunks (gain step/clamp, pct labels, five ASCII labels). CPGE's Configuration.h/.cpp already matched (user had edited both projects directly). History snapshots written (ScreenRecorder.cpp-03-07-2026-2.txt, ScreenRecorder.h/GUIConfigWindow.cpp/main.cpp-03-07-2026.txt); ReleaseInfo.md updated to v0.1.1947 under the July 03 2026 block. Identity constants verified untouched. Both projects fully in sync for the entire mic/recording bundle — NOTHING outstanding.

**GAME_NAME contamination FIXED (2026-07-03, explicit user instruction "fix it"):** CPGE2026's `CMakeLists.txt` line 9 had `set(GAME_NAME "TSOO" ...)` — residue of an earlier merge accident. Corrected to `"CPGE"` as a standalone fix (not part of a merge, so the never-touch-GAME_NAME merge rule did not apply). Verified consistent with `#define GAME_NAME "CPGE"` in CPGE's Includes.h and `<GameName>CPGE` in its vcxproj. History snapshot `CMakeLists.txt-03-07-2026.txt` written; ReleaseInfo.md bumped to v0.1.1948. Caveat for user: CMake CACHE variables persist — existing build folders still hold the cached "TSOO" value until reconfigured with a fresh cache (build/ is never touched by Claude per rules).

See also: merge-to-cpge-rules, feedback-2026-06-30-crash-investigation

---

## project-milestones.md

```yaml
---
name: project-milestones
description: TSOO project milestone log — completed feature milestones with dates.
metadata:
  type: project
  originSessionId: a9d9bfaa-957a-4135-8718-64a8ba0e30df
---
```

Milestone log for the TSOO engine project. Each entry is a confirmed working feature.

**Why:** User tracks progress as milestones; knowing what's already solid helps avoid re-investigating solved areas and frames suggestions relative to current completion state.

**How to apply:** Reference when discussing what's done vs. what's next; celebrate progress rather than treating everything as outstanding work.

## Milestones

| Date | Milestone |
|---|---|
| 2026-06-20 | **Emission Maps** — Blender GLTF/FBX emission texture pipeline fully working end-to-end (Principled BSDF Image Texture node → GLTF emissiveTexture → engine t7 slot). See blender-emission-export-guide. |
| 2026-06-29 | **CPGE→TSOO Merge** — XMLParser (DOM/SAX2/XPath), MPTMPlayer, S3MPlayer, ITPlayer, MODPlayer added; PUNPack updated (Blowfish+bcrypt); GUIConfigWindow cleaned up. Build system (CMakeLists, vcxproj), Includes.h, and main.cpp all fully wired. |
| 2026-07-03 | **User Profile Window Polish** — Commander roster portraitTexIndex now uses enum names (was off-by-one after IMG_TILESET1 shifted the enum); slider skips stat refresh while knob held on same profile; portrait ZoomFX (50% depth, 0.10 pace) on selection change AND on window open, with per-image stop (new `FXManager::StopZoomingImage`, `StopZooming(immediate)`); Panel draw routes through zoom FX in GUIManager when active + fully opaque; Quit-to-Desktop now fades in the quit-confirm dialog; new engine-generic `GUIControl::isReadOnly` makes stat HSliders display-only in TSOO (old `isClickHandled=false` never worked for sliders). All confirmed working by user. Merge classification (per user): ENGINE-level → StopZoomingImage/StopZooming(immediate), isReadOnly flag + HSlider handling, Panel zoom-draw hook, AND the Quit-to-Desktop → quit-confirm-dialog change incl. dialog fade-in (GameMenu + CreateQuitConfirmDialog are engine code). TSOO-only → profile-window wiring, portrait roster enum fix. |
| 2026-07-03 | **Screen Recorder Microphone — Fully Working** — Confirmed end-to-end: scored mic endpoint selection (headsets +100, mics +60, instrument/line adapters like the Rocksmith disqualified −1000), continuous mic capture even during silent game audio, WASAPI AUTOCONVERTPCM rate-lock (no more 44.1k/48k monitoring failures), 5-second peak-level diagnostic in DebugLog, timestamped `recording_YYYYMMDD_HHMMSS.mp4` output, mic volume range normalised to 0.0-1.0 across config window/OSD/Configuration, ASCII-only output strings. Hardware root cause of silent mic: half-seated headset plug, identified by the -61.5 dB diagnostic. All merged both directions — TSOO and CPGE in sync (CPGE v0.1.1948). See merge-log-2026-07-03-mic-scoring. |
| 2026-07-01 | **MPTM Playback System — Full IT/MPTM Compliance** — 20 defects fixed across two rounds. Key items: sample-accurate tick timing (MixAudio-driven), NNA virtual channels (backgroundVoices pool), ping-pong/S9F independence, delta-decode bit-depth wrapping, sample panning priority, panbrello basePanning, moduleGlobalVolume isolation, HardResume channel restore, ResolveSample instrument fallback, auto-vibrato sweep+apply, S7x NNA/envelope control, load-time unsupported-command warning, external sample forced to 44100Hz mono via MF, DirectSound pre-silenced, MPTM 127-channel support (was hard-capped at 64 causing aliasing), 32-bit accumulator mixing with Q15 gain chain replacing float buffer (correct IT-compatible loudness). |
| 2026-07-03 | **XM Playback — Full FT2 Command Compliance, Confirmed Working** — Root cause of notes "holding"/never stopping: Key Off (note 97) was completely unhandled in `TickRow()`, so looped-sample voices (traced to instruments 4/6/11 in battle.xm, 62 of 65 key-offs) never released and looped forever. Fixed with a real `ReleaseVoice()` (envelope release+fadeout if the instrument has a volume envelope, immediate cut otherwise). Also fixed while auditing the full effect table against battle.xm's actual usage (parsed directly from the binary, no build needed): Axx Volume Slide was wrongly applied on tick 0 as well as every tick; envelope fadeout was driven by audio-buffer callbacks instead of musical ticks (decoupled from BPM) — replaced with per-tick `UpdateEnvelopes()` (real envelope-point interpolation, sustain-hold, envelope loop, fadeout); Gxx/Hxy/Pxy effect numbers were mis-mapped to the wrong hex codes; Bxx Position Jump and Cxx Set Volume (effect column) were unimplemented; arpeggio/portamento-up/portamento-down/tone-portamento/vibrato had no real per-tick glide or memory; vibrato/tremolo/arpeggio/tremor pitch-or-volume changes never restored at the next row; EEx Pattern Delay was declared in the header but never wired up. User confirmed fully working after the fix. |

---

## reference-base-system.md

```yaml
---
name: reference-base-system
description: Location of the CPGE2026 base/reference project and the relationship between TSOO and CPGE — covers project structure, file comparison, and engine upgrade workflow.
metadata:
  type: reference
  originSessionId: 486f7964-1e3a-4900-9ce1-3a80ae4327aa
---
```

## Project Locations

- **TSOO (current project):** `f:\Projects\C++\TSOO`
- **CPGE Main Engine Core:** `f:\Projects\C++\CPGE2026` (one level up: `..\cpge2026`)

## Relationship

TSOO is a separate game project built on top of the CPGE engine. Its files are almost identical to CPGE2026, but TSOO contains game-specific additions isolated via the `PROJECT_ONLY_CODE` preprocessor directive defined at `Includes.h:65`.

## Engine Upgrade Workflow

When an engine upgrade is requested:
1. **Alter the files in the TSOO project first** (not CPGE2026).
2. **Wrap any TSOO-specific changes** in `#ifdef PROJECT_ONLY_CODE` / `#endif` unless explicitly told otherwise.
3. This ensures engine changes and game-specific changes stay isolated and can be ported back cleanly.

**Why:** The projects share a near-identical codebase. The `TSOO_ONLY_CODE` directive is the agreed boundary between engine-level code (shared) and game-level code (TSOO-only). Applying changes to TSOO first and guarding them with the directive prevents accidental contamination of the base engine.

## Debugging Crashes After File Imports

When TSOO crashes after importing updated files from CPGE2026, read both versions of the relevant file and diff them to find missing fixes. Pay particular attention to render frame files (`DXRenderFrame.cpp`, `DX12RenderFrame.cpp`, `OpenGLRenderFrame.cpp`, `VULKAN_RenderFrame.cpp`), `SceneManager`, `ThreadManager`, and any animator files.

## Note on TSOO_ONLY_CODE

`TSOO_ONLY_CODE` does **not** exist in the project. The actual project-isolation directive is `PROJECT_ONLY_CODE`, defined as a plain `#define` at `Includes.h:65`. This is absent from CPGE2026's Includes.h, so `#ifdef PROJECT_ONLY_CODE` blocks are automatically stripped when building the base engine.

---

## blender-emission-export-guide.md

```yaml
---
name: blender-emission-export-guide
description: "Emission maps confirmed working (milestone 2026-06-20). Root cause + fix for SceneManager emissiveFactor-but-no-emissiveTexture warning; Blender 5.1.2 GLTF & FBX export guide."
metadata:
  type: reference
  originSessionId: 57db5680-b2c0-4de8-a671-386053f0d7b8
---
```

## Status: RESOLVED — 2026-06-20

Emission maps are working as expected. Milestone reached. See project-milestones for the full milestone log.

## The Warning

```
[SceneManager] Model[N] material[M] "name": emissiveFactor=(1.00,1.00,1.00)x2.20
but NO emissiveTexture in GLTF -- shader will emit solid colour.
```

Triggered in SceneManager.cpp ~line 3948 when `emissiveFactor` is non-black but `emissiveTexture` is absent.

**Root cause:** Blender only writes `emissiveTexture` to GLTF when an **Image Texture node** is connected to the Emission Color socket of Principled BSDF. A plain flat color only writes `emissiveFactor` — no texture entry is produced.

### Shader Editor Setup (Required for Both Formats)

```
[Image Texture]
  Color Space: sRGB
  Color output ──→ Emission Color  [Principled BSDF]
                   Emission Strength = <value, e.g. 2.20>
```

- Must use an Image Texture node — flat color alone is not enough.
- Color Space on the node must be **sRGB** (not Non-Color).
- Do NOT wire it to a separate Emission shader node; use Principled BSDF's own Emission Color socket.

### GLTF/GLB Export (Preferred Path)

File → Export → glTF 2.0 (.glb/.gltf)

| Setting | Value |
|---|---|
| Materials | Export |
| Images | Automatic (or PNG/JPEG) |
| Extensions → KHR_materials_emissive_strength | **Ticked** (required when Emission Strength > 1.0) |

Result: GLTF contains both `emissiveTexture` and `emissiveFactor`. Engine populates `emissiveMapPath` → `emissiveMap` → `useEmissiveMap = true` → t7 texture slot.

### FBX Export

File → Export → FBX (.fbx)

| Setting | Value |
|---|---|
| Export Textures | Ticked |
| Path Mode | Copy + embed icon active |

**Caveat:** FBX has no native PBR emission slot. Blender writes to the FBX `Emissive` channel; the engine reads it via `emissiveTexID` in FBXImport.h:221. GLTF is more reliable for PBR emission.

### Engine File References

| File | What it does |
|---|---|
| SceneManager.cpp ~3938–3952 | Emits the warning; comment explains the Blender bug |
| BlenderImports.cpp ~273 | Parses `emissiveFactor` from GLTF JSON |
| Models.h:211,220,388 | `emissiveMapPath`, `emissiveMap`, `useEmissiveMap` fields |
| Includes.h:712 | `SLOT_emissiveMap = 7` (t7 texture slot) |
| ConstantBuffer.h:63–68 | `EmissiveFactor`, `EmissiveStrength`, `useEmissiveMap` GPU constants |
| FBXImport.h:221 | `emissiveTexID` for FBX path |

### Quick Checklist Before Every Export

- Image Texture node exists (not just a flat color)
- Color output → Principled BSDF Emission Color
- Color Space = sRGB on Image Texture node
- Emission Strength > 0 on Principled BSDF
- (GLTF) KHR_materials_emissive_strength ticked if Strength > 1.0
- (FBX) Path Mode = Copy + embed icon active

---

*End of dump — 15 memory files (1 index + 14 entries), reproduced in full without summarization or omission.*
