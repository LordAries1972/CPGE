# ThreadManager and ThreadLockHelper Usage Guide

## Table of Contents

1. [Introduction](#introduction)
2. [ThreadManager Class Overview](#threadmanager-class-overview)
3. [ThreadLockHelper Overview](#threadlockhelper-overview)
4. [Prerequisites](#prerequisites)
5. [Basic Setup](#basic-setup)

6. [ThreadManager Usage](#threadmanager-usage)
   - [6.1 Creating and Starting Threads](#61-creating-and-starting-threads)
   - [6.2 Thread Control Operations](#62-thread-control-operations)
   - [6.3 Thread Status Management](#63-thread-status-management)
   - [6.4 Thread Variables and Shared State](#64-thread-variables-and-shared-state)
   - [6.5 Thread Debug Logging](#65-thread-debug-logging)

7. [Thread Scheduling and Affinity (Windows)](#thread-scheduling-and-affinity-windows)
   - [7.1 InitialiseThreadAffinity](#71-initialisethreadaffinity)
   - [7.2 ThreadSchedulingConfig](#72-threadschedulingconfig)
   - [7.3 ThreadUtils](#73-threadutils)

8. [Lock Management](#lock-management)
   - [8.1 Creating and Managing Locks](#81-creating-and-managing-locks)
   - [8.2 Using ThreadLockHelper (RAII)](#82-using-threadlockhelper-raii)
   - [8.3 Multiple Lock Management](#83-multiple-lock-management)

9. [Complete Examples](#complete-examples)
   - [9.1 Basic Thread Example](#91-basic-thread-example)
   - [9.2 Renderer Thread Example](#92-renderer-thread-example)
   - [9.3 Thread Synchronization Example](#93-thread-synchronization-example)
   - [9.4 Producer-Consumer Pattern](#94-producer-consumer-pattern)

10. [Best Practices](#best-practices)
11. [Error Handling](#error-handling)
12. [Debug Information](#debug-information)
13. [Thread Safety Guidelines](#thread-safety-guidelines)
14. [Common Pitfalls](#common-pitfalls)
15. [Advanced Usage](#advanced-usage)
16. [API Reference](#api-reference)

---

## Introduction

The ThreadManager class provides a comprehensive thread management system for game engines and multi-threaded applications. It simplifies thread creation, control, and synchronization while providing robust lock management capabilities through the ThreadLockHelper RAII wrapper.

On Windows platforms the engine also exposes fine-grained scheduling control via `ThreadSchedulingConfig`, the `ThreadUtils` static utility class, and `InitialiseThreadAffinity()` — all covered in [Section 7](#thread-scheduling-and-affinity-windows).

This guide covers both basic and advanced usage patterns, complete with working examples and best practices for production environments.

---

## ThreadManager Class Overview

The ThreadManager class offers:

- **Thread Creation & Management**: Named threads with lifecycle control
- **Thread Status Monitoring**: Real-time status tracking and debugging
- **Lock Management**: Thread-safe lock creation and synchronization with re-entrant support
- **Graceful Shutdown**: Safe cleanup and resource management
- **Shared Variables**: Thread-safe shared state management via `ThreadVariables`
- **Windows Scheduling**: Ideal-processor hints, priority control, and thread naming for profilers

### Key Features:

- Named thread identification using `ThreadNameID` enum
- Automatic thread lifecycle management
- RAII-style lock management via `ThreadLockHelper`
- Re-entrant locking — same thread can re-acquire its own lock without deadlock
- Debug logging integration (conditional via `_DEBUG_THREADMANAGER_`)
- Thread-safe operations with mutex protection
- Windows: thread-to-LP assignment, priority scheduling, and debugger name registration

---

## ThreadLockHelper Overview

`ThreadLockHelper` provides RAII (Resource Acquisition Is Initialization) style lock management:

- **Automatic Lock Acquisition**: Lock acquired in constructor
- **Automatic Release**: Lock released in destructor
- **Timeout Support**: Configurable lock acquisition timeouts
- **Multiple Lock Support**: `MultiThreadLockHelper` manages multiple locks safely
- **Exception Safety**: Locks automatically released on exceptions
- **Re-entrant Safe**: If the calling thread already owns the lock, `TryLock` increments a depth counter instead of blocking

---

## Prerequisites

### Required Headers:
```cpp
#include "ThreadManager.h"
#include "Debug.h"
```

### Global Instances:
```cpp
extern ThreadManager threadManager;
extern Debug debug;
```

### Thread Name Registration:
Ensure your thread names are registered in the `ThreadNameID` enum and corresponding `getThreadName()` function in ThreadManager.cpp.

---

## Basic Setup

### 1. Include Required Headers
```cpp
#include "ThreadManager.h"
```

### 2. Access Global ThreadManager Instance
```cpp
// ThreadManager is typically declared as a global singleton
extern ThreadManager threadManager;
```

### 3. Initialize Thread Affinity (Windows — before first SetThread)

```cpp
// On Windows, call this once at startup before creating any threads.
// Queries LP count and logs the recommended engine thread layout.
threadManager.InitialiseThreadAffinity();
```

### 4. ThreadManager initializes automatically upon construction

```cpp
// No further initialization required — constructor sets all defaults.
```

---

## ThreadManager Usage

### 6.1 Creating and Starting Threads

#### Basic Thread Creation
```cpp
void CreateRenderingThread() {
    // Define the thread task
    auto renderTask = []() {
        while (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running) {
            // Perform rendering operations
            if (threadManager.threadVars.bIsShuttingDown.load()) {
                break;
            }
            
            // Your rendering logic here
            RenderFrame();
            
            // Sleep to control frame rate
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }
    };
    
    // Create the thread (engine assigns LP 2, ABOVE_NORMAL priority automatically on Windows)
    threadManager.SetThread(THREAD_RENDERER, renderTask);
}
```

#### Thread with Error Handling
```cpp
void CreateNetworkThread() {
    auto networkTask = []() {
        try {
            while (threadManager.GetThreadStatus(THREAD_NETWORK) == ThreadStatus::Running) {
                // Check for shutdown request
                if (threadManager.threadVars.bIsShuttingDown.load()) {
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Network thread shutting down gracefully");
                    break;
                }
                
                // Network operations
                ProcessNetworkMessages();
                
                // Yield to other threads
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Network thread exception: %hs", e.what());
        }
    };
    
    threadManager.SetThread(THREAD_NETWORK, networkTask);
}
```

### 6.2 Thread Control Operations

#### Starting Threads
```cpp
void StartEngineThreads() {
    // Check if thread exists before starting
    if (threadManager.DoesThreadExist(THREAD_RENDERER)) {
        threadManager.StartThread(THREAD_RENDERER);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Renderer thread started");
    }
    
    if (threadManager.DoesThreadExist(THREAD_NETWORK)) {
        threadManager.StartThread(THREAD_NETWORK);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Network thread started");
    }
}
```

#### Pausing and Resuming Threads
```cpp
void PauseGameSystems() {
    // Pause non-critical threads during loading
    threadManager.PauseThread(THREAD_AI_PROCESSING);
    threadManager.PauseThread(THREAD_NETWORK);
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Game systems paused for loading");
}

void ResumeGameSystems() {
    // Resume threads after loading
    threadManager.ResumeThread(THREAD_AI_PROCESSING);
    threadManager.ResumeThread(THREAD_NETWORK);
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Game systems resumed");
}
```

#### Graceful Thread Shutdown
```cpp
void ShutdownEngineThreads() {
    // Set shutdown flag first
    threadManager.threadVars.bIsShuttingDown.store(true);
    
    // Stop individual threads
    threadManager.StopThread(THREAD_RENDERER);
    threadManager.StopThread(THREAD_NETWORK);
    threadManager.StopThread(THREAD_AI_PROCESSING);
    
    // Wait a moment for graceful shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Clean up all threads
    threadManager.Cleanup();
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"All engine threads shut down");
}
```

### 6.3 Thread Status Management

#### Checking Thread Status
```cpp
void MonitorThreadHealth() {
    // Check individual thread status
    ThreadStatus rendererStatus = threadManager.GetThreadStatus(THREAD_RENDERER);
    ThreadStatus networkStatus  = threadManager.GetThreadStatus(THREAD_NETWORK);
    
    switch (rendererStatus) {
        case ThreadStatus::Running:
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Renderer thread is running normally");
            break;
        case ThreadStatus::Paused:
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Renderer thread is paused");
            break;
        case ThreadStatus::Stopped:
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Renderer thread has stopped");
            // Restart if needed
            threadManager.StartThread(THREAD_RENDERER);
            break;
        case ThreadStatus::Terminated:
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Renderer thread was terminated");
            break;
    }
}
```

#### Thread Information Access
```cpp
void LogThreadInformation() {
    // Get thread ID for debugging
    std::thread::id renderThreadID = threadManager.GetThreadID(THREAD_RENDERER);
    
    // Check debug mode
    bool isDebugMode = threadManager.IsDebugMode(THREAD_RENDERER);
    
    // Get complete thread info (unsafe — only call with threadsMutex already locked
    // or when no other threads are modifying the map)
    ThreadInfo& info = threadManager.GetThreadInfoUnsafe(THREAD_RENDERER);
    
    debug.logDebugMessage(LogLevel::LOG_DEBUG,
        L"Renderer thread - Status: %d, Debug: %s",
        static_cast<int>(info.status),
        isDebugMode ? L"enabled" : L"disabled");
}
```

### 6.4 Thread Variables and Shared State

`ThreadVariables` is a singleton accessed via `threadManager.threadVars`. It holds atomic status flags shared across all threads. All fields are `std::atomic<bool>` and default-initialized in the constructor.

#### Complete ThreadVariables Field Reference

| Field | Default | Description |
| --- | --- | --- |
| `bLoaderTaskFinished` | `true` | `false` while resources are being loaded |
| `bIsRendering` | `false` | `true` while a frame is actively being rendered |
| `bIsShuttingDown` | `false` | `true` when application shutdown is in progress |
| `bIsResizing` | `false` | `true` while window/swap-chain resize is in progress |
| `bHasGameReset` | `false` | Set to `true` to signal a game-state reset |
| `bHasReset` | `false` | General-purpose reset flag for engine sub-systems |
| `b2DTexturesLoaded` | `false` | `true` once 2D renderer textures are loaded into GPU memory |
| `bSettingFullScreen` | `false` | `true` while a full-screen mode transition is in progress |
| `bInitiateFader` | `false` | `true` to trigger the loading-screen fade effect |

#### Using Atomic Variables
```cpp
void ManageSharedState() {
    auto& vars = threadManager.threadVars;
    
    // Signal that rendering has started
    vars.bIsRendering.store(true);
    
    // Check if loader has finished
    if (vars.bLoaderTaskFinished.load()) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Loader task completed");
        // Continue with main game loop
    }
    
    // Pause rendering during window resize
    if (vars.bIsResizing.load()) {
        vars.bIsRendering.store(false);
    }
    
    // Trigger loading-screen fade
    vars.bInitiateFader.store(true);
    
    // Signal a full-screen mode change
    vars.bSettingFullScreen.store(true);
    
    // Reset game state
    vars.bHasGameReset.store(true);
}
```

### 6.5 Thread Debug Logging

ThreadManager uses a compile-time conditional macro system. By default **all debug output is compiled out** — zero runtime cost — unless both `_DEBUG_THREADMANAGER_` and `_DEBUG` are defined simultaneously.

#### Enabling Verbose ThreadManager Logs

Add the define in your precompiled header or via the compiler command line:
```cpp
// In your precompiled header (before including ThreadManager.h):
#define _DEBUG_THREADMANAGER_
```
Or add `/D_DEBUG_THREADMANAGER_` to your debug build compiler flags.

When both conditions are met, the internal macros expand to:
- `TM_LOG_LEVEL(lvl, msg)` → `debug.logLevelMessage(lvl, msg)`
- `TM_LOG_DEBUG(lvl, fmt, ...)` → `debug.logDebugMessage(lvl, fmt, ...)`

When disabled, both macros compile to `(void)` no-ops with no overhead.

This means ThreadManager never produces debug noise in release builds without any extra `#ifdef` guards in calling code.

---

## Thread Scheduling and Affinity (Windows)

On Windows builds (`PLATFORM_WINDOWS` defined), the engine provides fine-grained control over thread-to-processor assignment and CPU priority via three mechanisms. These features are compiled out entirely on non-Windows platforms.

### 7.1 InitialiseThreadAffinity

`InitialiseThreadAffinity()` must be called **once at engine startup, before the first `SetThread()` call**. It queries the system logical processor (LP) count, validates that the recommended engine thread layout can be satisfied, and logs a full LP assignment table to the debug output.

```cpp
void EngineStartup() {
    // Call before any SetThread()
    bool affOk = threadManager.InitialiseThreadAffinity();
    if (!affOk) {
        // Returns false on single-core systems where hints are not applicable
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Single-core system -- affinity hints skipped");
    }
    
    // Now safe to create threads
    threadManager.SetThread(THREAD_RENDERER, renderTask);
    threadManager.SetThread(THREAD_LOADER,   loaderTask);
}
```

The function logs the following layout table on startup:

| LP | Thread | Default Priority |
| --- | --- | --- |
| 0 | Main thread / OS / Window message pump | (reserved) |
| 1 | GE-AI-Thread | `THREAD_PRIORITY_NORMAL` |
| 2 | GE-Rendering-Thread | `THREAD_PRIORITY_ABOVE_NORMAL` |
| 3 | GE-Loader-Thread | `THREAD_PRIORITY_NORMAL` |
| 4 | GE-FileIO-Processing-Thread | `THREAD_PRIORITY_NORMAL` |
| 5 | Audio / Worker jobs | `THREAD_PRIORITY_HIGHEST` (set in SoundManager) |

**Returns:** `true` if 2+ LPs are available; `false` on single-core systems. Non-Windows platforms always return `false` and skip silently.

### 7.2 ThreadSchedulingConfig

`ThreadSchedulingConfig` is an optional struct passed as the last parameter to `SetThread()` on Windows. When `useEngineDefaults = true` (the default), the engine automatically fills in the correct LP and priority for each known `ThreadNameID` — you do not need to set anything manually for standard engine threads.

```cpp
// Default: engine auto-assigns LP and priority (recommended)
threadManager.SetThread(THREAD_RENDERER, renderTask);
threadManager.SetThread(THREAD_LOADER,   loaderTask, false);  // false = release debug mode

// Explicit override: manually specify LP and priority
ThreadSchedulingConfig cfg;
cfg.useEngineDefaults = false;
cfg.idealCore         = 3;                           // Prefer LP 3 (soft hint)
cfg.priority          = THREAD_PRIORITY_ABOVE_NORMAL;
threadManager.SetThread(THREAD_RENDERER, renderTask, false, cfg);
```

#### ThreadSchedulingConfig Fields

| Field | Default | Description |
| --- | --- | --- |
| `idealCore` | `MAXDWORD` | Logical processor index for `SetThreadIdealProcessor()`. `MAXDWORD` = use engine default for the thread type. |
| `priority` | `THREAD_PRIORITY_NORMAL` | Win32 priority constant. Avoid `THREAD_PRIORITY_TIME_CRITICAL` in production. |
| `useEngineDefaults` | `true` | When `true`, overrides `idealCore`/`priority` with the built-in per-thread-type CPGE defaults. |

**Important:** The config is applied from **inside** the new thread, not from the creating thread. This is required because `GetCurrentThread()` only resolves to the correct handle when called from within the thread itself.

#### Built-in engine defaults (applied when `useEngineDefaults = true`):
```
THREAD_AI_PROCESSING  -> LP 1, THREAD_PRIORITY_NORMAL
THREAD_RENDERER       -> LP 2, THREAD_PRIORITY_ABOVE_NORMAL
THREAD_LOADER         -> LP 3, THREAD_PRIORITY_NORMAL
THREAD_FILEIO         -> LP 4, THREAD_PRIORITY_NORMAL
THREAD_NETWORK        -> LP 5, THREAD_PRIORITY_NORMAL
```

### 7.3 ThreadUtils

`ThreadUtils` is a static utility class for Windows-only thread scheduling and naming. **All methods operate on the calling thread** (`GetCurrentThread()`) — they must be called from **inside** the thread function body, never from the thread that created it.

The engine calls `ThreadUtils::NameCurrentThread()`, `PreferCore()`, and `SetPriority()` automatically via `SetThread()` when `PLATFORM_WINDOWS` is defined. The methods below are also available for user-defined threads or custom scheduling scenarios.

#### NameCurrentThread
Registers a descriptive name visible in Visual Studio 2022 debugger, PIX for Windows, RenderDoc, and Windows Performance Analyzer (WPA).

```cpp
auto renderTask = []() {
    // Call first thing inside the thread body
    ThreadUtils::NameCurrentThread(L"MyCustomRenderer");
    // Thread now appears with this name in VS 2022 and GPU profilers
    
    while (/* running */) {
        RenderFrame();
    }
};
```

Requires Windows 10 version 1607 (Build 14393) or later (`SetThreadDescription` API).

#### PreferCore — Recommended for Production
Sets a **soft scheduling hint** via `SetThreadIdealProcessor()`. The Windows kernel will prefer this LP but may migrate the thread for load balancing, thermal management, or Intel Thread Director policy.

```cpp
auto loaderTask = []() {
    // Prefer LP 3 (soft hint — OS can override for thermal/power management)
    bool ok = ThreadUtils::PreferCore(3);
    if (!ok) {
        // LP 3 is not available on this machine (e.g., only 2 LPs exist)
    }
    
    while (/* running */) {
        LoadNextResource();
    }
};
```

Returns `false` if `coreIndex` exceeds the available LP count.

#### ForceCore — Debug and Profiling Only
Hard-locks the thread to a single LP via `SetThreadAffinityMask()`. Completely prevents Windows from migrating the thread for **any** reason.

```cpp
// WARNING: Only use during benchmarking and profiling — NEVER in production
auto profilingTask = []() {
    ThreadUtils::ForceCore(2);    // Hard-pin to LP 2
    // Windows load balancing, Intel Thread Director, and Ryzen multi-CCD
    // distribution are all disabled for this thread
    
    RunPerformanceBenchmark();
};
```

**Disables:**
- Windows kernel load balancing
- Intel Thread Director (P-core / E-core placement on hybrid CPUs)
- Ryzen multi-CCD thread distribution

#### SetPriority
Adjusts the scheduling weight of the calling thread.

```cpp
auto audioTask = []() {
    ThreadUtils::SetPriority(THREAD_PRIORITY_HIGHEST);
    // Valid constants: THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
    //   THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL,
    //   THREAD_PRIORITY_HIGHEST
    // NEVER use THREAD_PRIORITY_TIME_CRITICAL in production --
    //   it can preempt OS audio drivers and the Windows message pump
    
    while (/* running */) {
        ProcessAudio();
    }
};
```

#### GetLogicalProcessorCount
Returns the system LP count via `GetSystemInfo()`. Includes all P-cores, E-cores, and hyper-threaded siblings.

```cpp
DWORD lpCount = ThreadUtils::GetLogicalProcessorCount();
debug.logDebugMessage(LogLevel::LOG_INFO,
    L"System has %u logical processor(s)", lpCount);
// On a 6P+4E Intel core with HT: reports 16 LPs
```

---

## Lock Management

### 8.1 Creating and Managing Locks

#### Manual Lock Management
```cpp
void ManualLockExample() {
    const std::string lockName = "resource_access_lock";
    
    // Create a lock (sets this thread as owner, marks as locked)
    if (threadManager.CreateLock(lockName)) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Lock created successfully");
        
        // Perform thread-safe operations
        AccessSharedResource();
        
        // Remove the lock when done (only the owner thread can remove)
        threadManager.RemoveLock(lockName);
    } else {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to create lock - may already exist");
    }
}
```

#### Try Lock with Timeout
```cpp
void TryLockExample() {
    const std::string lockName = "critical_section_lock";
    const int timeoutMs = 1000; // 1 second timeout
    
    // TryLock creates the lock if it doesn't exist, or waits for it if another thread owns it.
    // Re-entrant: if the calling thread already owns it, lockCount is incremented (no block).
    if (threadManager.TryLock(lockName, timeoutMs)) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Lock acquired successfully");
        
        // Perform critical section operations
        ProcessCriticalData();
        
        // Release the lock
        threadManager.RemoveLock(lockName);
    } else {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock within timeout");
        // Handle timeout case
    }
}
```

#### Re-entrant Locking
The same thread can call `TryLock` on a lock it already owns without blocking. Each re-entrant acquisition increments `lockCount`; each `RemoveLock` decrements it. The lock is only truly freed when the count reaches zero.

```cpp
void ReentrantExample() {
    const std::string lock = "my_lock";
    
    // First acquisition — creates the lock, lockCount = 1
    if (threadManager.TryLock(lock, 1000)) {
        
        // Nested re-entrant acquisition — same thread, lockCount = 2, no block
        if (threadManager.TryLock(lock, 1000)) {
            DoNestedWork();
            threadManager.RemoveLock(lock);  // lockCount -> 1, still locked
        }
        
        DoOuterWork();
        threadManager.RemoveLock(lock);  // lockCount -> 0, lock freed
    }
}
```

### 8.2 Using ThreadLockHelper (RAII)

#### Basic RAII Lock Usage
```cpp
void RAIILockExample() {
    // Lock is automatically acquired in constructor via TryLock()
    ThreadLockHelper lock(threadManager, "my_resource_lock", 1000);
    
    // Check if lock was acquired
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire lock");
        return;
    }
    
    // Perform thread-safe operations
    ModifySharedResource();
    
    // Lock is automatically released when 'lock' goes out of scope
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Critical section completed");
}
```

#### Silent Lock Acquisition
```cpp
void SilentLockExample() {
    // silent = true: no warning is logged if the lock times out
    ThreadLockHelper lock(threadManager, "background_task_lock", 500, true);
    
    if (lock.IsLocked()) {
        // Perform background operations
        ProcessBackgroundTask();
    }
}
```

#### Manual Lock Release
```cpp
void ManualReleaseExample() {
    ThreadLockHelper lock(threadManager, "file_access_lock", 2000);
    
    if (lock.IsLocked()) {
        // Perform first operation
        WriteToFile();
        
        // Manually release lock early so other threads can proceed
        lock.Release();
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Lock released early");
        // Other threads can now acquire "file_access_lock"
    }
    
    // Destructor is a no-op when Release() was already called
}
```

### 8.3 Multiple Lock Management

#### Sequential Multiple Locks
```cpp
void MultiLockExample() {
    MultiThreadLockHelper locks(threadManager);
    
    // Acquire locks in sequence. If any lock fails, ALL previously acquired locks
    // are released automatically before returning.
    if (!locks.TryLock("database_lock")    ||
        !locks.TryLock("file_system_lock") ||
        !locks.TryLock("network_lock")) {
        
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire all required locks");
        return; // All locks automatically released
    }
    
    // All locks acquired successfully
    ComplexOperation();
    
    // All locks automatically released in LIFO order when 'locks' goes out of scope
}
```

---

## Complete Examples

### 9.1 Basic Thread Example

```cpp
class BasicThreadExample {
public:
    void StartExample() {
        auto workerTask = []() {
            int workCount = 0;
            
            while (threadManager.GetThreadStatus(THREAD_FILEIO) == ThreadStatus::Running) {
                if (threadManager.threadVars.bIsShuttingDown.load()) {
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Worker thread shutting down");
                    break;
                }
                
                PerformWork(workCount++);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        };
        
        threadManager.SetThread(THREAD_FILEIO, workerTask);
        threadManager.StartThread(THREAD_FILEIO);
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Basic worker thread started");
    }
    
private:
    static void PerformWork(int workId) {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Performing work item %d", workId);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};
```

### 9.2 Renderer Thread Example

```cpp
class RendererThreadExample {
private:
    std::atomic<bool> m_renderingEnabled{true};
    std::atomic<int>  m_frameCount{0};
    
public:
    void StartRenderer() {
        auto renderTask = [this]() {
            auto lastFrameTime = std::chrono::high_resolution_clock::now();
            
            while (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running) {
                // Handle pause
                if (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Paused) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                
                if (threadManager.threadVars.bIsShuttingDown.load()) break;
                
                // Skip rendering during window resize
                if (threadManager.threadVars.bIsResizing.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                
                RenderFrameThreadSafe();
                
                // Frame rate control (~60 FPS)
                auto currentTime  = std::chrono::high_resolution_clock::now();
                auto frameDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastFrameTime);
                
                if (frameDuration < std::chrono::milliseconds(16)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(16) - frameDuration);
                }
                
                lastFrameTime = std::chrono::high_resolution_clock::now();
                m_frameCount++;
            }
            
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Renderer thread completed %d frames",
                m_frameCount.load());
        };
        
        threadManager.SetThread(THREAD_RENDERER, renderTask);
        threadManager.StartThread(THREAD_RENDERER);
    }
    
private:
    void RenderFrameThreadSafe() {
        ThreadLockHelper renderLock(threadManager, "render_context_lock", 100);
        if (!renderLock.IsLocked()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Skipping frame - render lock unavailable");
            return;
        }
        
        threadManager.threadVars.bIsRendering.store(true);
        
        try {
            ExecuteRenderCommands();
            SwapBuffers();
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Render error: %hs", e.what());
        }
        
        threadManager.threadVars.bIsRendering.store(false);
        // Lock released automatically here
    }
    
    void ExecuteRenderCommands() {
        std::this_thread::sleep_for(std::chrono::microseconds(8000));
    }
    
    void SwapBuffers() {
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }
};
```

### 9.3 Thread Synchronization Example

```cpp
class ThreadSynchronizationExample {
private:
    std::queue<int> m_dataQueue;
    const std::string QUEUE_LOCK = "data_queue_lock";
    
public:
    void StartProducerConsumer() {
        auto producerTask = [this]() {
            int dataValue = 0;
            
            while (threadManager.GetThreadStatus(THREAD_AI_PROCESSING) == ThreadStatus::Running) {
                if (threadManager.threadVars.bIsShuttingDown.load()) break;
                
                ProduceData(dataValue++);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        };
        
        auto consumerTask = [this]() {
            while (threadManager.GetThreadStatus(THREAD_FILEIO) == ThreadStatus::Running) {
                if (threadManager.threadVars.bIsShuttingDown.load()) break;
                
                ConsumeData();
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        };
        
        threadManager.SetThread(THREAD_AI_PROCESSING, producerTask);
        threadManager.SetThread(THREAD_FILEIO,        consumerTask);
        
        threadManager.StartThread(THREAD_AI_PROCESSING);
        threadManager.StartThread(THREAD_FILEIO);
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Producer-Consumer threads started");
    }
    
private:
    void ProduceData(int value) {
        ThreadLockHelper lock(threadManager, QUEUE_LOCK, 1000);
        if (!lock.IsLocked()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Producer: Failed to acquire queue lock");
            return;
        }
        
        m_dataQueue.push(value);
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Produced: %d (Queue size: %zu)",
            value, m_dataQueue.size());
    }
    
    void ConsumeData() {
        ThreadLockHelper lock(threadManager, QUEUE_LOCK, 1000);
        if (!lock.IsLocked()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Consumer: Failed to acquire queue lock");
            return;
        }
        
        if (!m_dataQueue.empty()) {
            int value = m_dataQueue.front();
            m_dataQueue.pop();
            
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Consumed: %d (Queue size: %zu)",
                value, m_dataQueue.size());
            
            // Release early so producer can continue while we process
            lock.Release();
            
            ProcessData(value);
        }
    }
    
    void ProcessData(int value) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processed data: %d", value);
    }
};
```

### 9.4 Producer-Consumer Pattern

```cpp
class ResourceManager {
private:
    std::vector<std::string> m_pendingLoads;
    std::vector<std::string> m_loadedResources;
    const std::string LOAD_QUEUE_LOCK   = "resource_load_queue";
    const std::string LOADED_LIST_LOCK  = "loaded_resources_list";
    std::atomic<bool> m_loadingComplete{false};
    
public:
    void StartResourceLoading(const std::vector<std::string>& resources) {
        // Initialize pending loads
        {
            ThreadLockHelper lock(threadManager, LOAD_QUEUE_LOCK, 2000);
            if (lock.IsLocked()) {
                m_pendingLoads = resources;
            }
        }
        
        auto loaderTask = [this]() {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Resource loader thread started");
            
            while (threadManager.GetThreadStatus(THREAD_LOADER) == ThreadStatus::Running) {
                if (threadManager.threadVars.bIsShuttingDown.load()) break;
                
                std::string resourceToLoad;
                bool hasWork = false;
                
                {
                    ThreadLockHelper lock(threadManager, LOAD_QUEUE_LOCK, 100);
                    if (lock.IsLocked() && !m_pendingLoads.empty()) {
                        resourceToLoad = m_pendingLoads.back();
                        m_pendingLoads.pop_back();
                        hasWork = true;
                    }
                }
                
                if (hasWork) {
                    if (LoadResource(resourceToLoad)) {
                        ThreadLockHelper loadedLock(threadManager, LOADED_LIST_LOCK, 1000);
                        if (loadedLock.IsLocked()) {
                            m_loadedResources.push_back(resourceToLoad);
                        }
                    }
                } else {
                    bool stillLoading = false;
                    {
                        ThreadLockHelper lock(threadManager, LOAD_QUEUE_LOCK, 100);
                        if (lock.IsLocked()) {
                            stillLoading = !m_pendingLoads.empty();
                        }
                    }
                    
                    if (!stillLoading) {
                        m_loadingComplete.store(true);
                        threadManager.threadVars.bLoaderTaskFinished.store(true);
                        debug.logLevelMessage(LogLevel::LOG_INFO, L"All resources loaded");
                        break;
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        };
        
        threadManager.SetThread(THREAD_LOADER, loaderTask);
        threadManager.StartThread(THREAD_LOADER);
    }
    
    std::vector<std::string> GetLoadedResources() {
        ThreadLockHelper lock(threadManager, LOADED_LIST_LOCK, 1000);
        if (lock.IsLocked()) {
            return m_loadedResources; // Copy
        }
        return {};
    }
    
    bool IsLoadingComplete() const {
        return m_loadingComplete.load();
    }
    
private:
    bool LoadResource(const std::string& resourceName) {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Loading resource: %hs", resourceName.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(100 + (rand() % 200)));
        
        bool success = (rand() % 10) != 0; // 90% success rate
        
        if (success)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Successfully loaded: %hs", resourceName.c_str());
        else
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Failed to load: %hs", resourceName.c_str());
        
        return success;
    }
};
```

---

## Best Practices

### 1. Call InitialiseThreadAffinity Before SetThread (Windows)
```cpp
// Good: Affinity report logged, engine knows LP count before threads start
threadManager.InitialiseThreadAffinity();
threadManager.SetThread(THREAD_RENDERER, renderTask);

// Bad: Scheduler operates without validated LP count
threadManager.SetThread(THREAD_RENDERER, renderTask);  // LP count not queried
```

### 2. Always Check Thread Status
```cpp
// Good: Check status before operations
if (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running) {
    // Perform thread operations
}

// Bad: Assume thread is running
// PerformThreadOperations(); // May fail if thread is paused/stopped
```

### 3. Use RAII Locks Whenever Possible
```cpp
// Good: RAII lock management
{
    ThreadLockHelper lock(threadManager, "resource_lock", 1000);
    if (lock.IsLocked()) {
        AccessSharedResource();
    }
    // Lock automatically released
}

// Avoid: Manual lock management (error-prone if exception is thrown)
threadManager.CreateLock("resource_lock");
AccessSharedResource(); // What if exception occurs here?
threadManager.RemoveLock("resource_lock"); // May not be reached
```

### 4. Handle Shutdown Gracefully
```cpp
// Good: Check shutdown flag regularly
while (threadManager.GetThreadStatus(myThread) == ThreadStatus::Running) {
    if (threadManager.threadVars.bIsShuttingDown.load()) {
        break; // Exit gracefully
    }
    
    DoWork();
}

// Bad: Ignore shutdown requests
while (true) {
    DoWork(); // Thread may never exit cleanly
}
```

### 5. Use Appropriate Lock Timeouts
```cpp
// Good: Reasonable timeout for critical operations
ThreadLockHelper lock(threadManager, "critical_lock", 2000); // 2 seconds

// Good: Short timeout for optional operations
ThreadLockHelper lock(threadManager, "optional_lock", 100); // 100ms

// Bad: Zero timeout (returns immediately if contested -- usually wrong)
ThreadLockHelper lock(threadManager, "risky_lock", 0);
```

### 6. Minimize Lock Scope
```cpp
// Good: Narrow lock scope
void ProcessData() {
    PrepareData();  // Outside lock
    
    {
        ThreadLockHelper lock(threadManager, "data_lock", 1000);
        if (lock.IsLocked()) {
            ModifySharedData(); // Only the critical section is locked
        }
    }
    
    PostProcessData(); // Outside lock
}

// Bad: Wide lock scope
void ProcessDataBad() {
    ThreadLockHelper lock(threadManager, "data_lock", 1000);
    if (lock.IsLocked()) {
        PrepareData();      // Unnecessary lock holding
        ModifySharedData(); // Critical section
        PostProcessData();  // Unnecessary lock holding
    }
}
```

### 7. Use PreferCore Not ForceCore in Production (Windows)
```cpp
// Good: Soft hint — OS can still manage threads for thermal/power
ThreadUtils::PreferCore(2);

// Bad: Hard affinity in production — disables OS load balancing
ThreadUtils::ForceCore(2); // BENCHMARKING AND PROFILING ONLY
```

---

## Error Handling

### Thread Creation Errors
```cpp
void SafeThreadCreation() {
    try {
        auto threadTask = []() {
            // Thread implementation
        };
        
        // Check if thread already exists before creating
        if (threadManager.DoesThreadExist(THREAD_NETWORK)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Thread already exists");
            return;
        }
        
        threadManager.SetThread(THREAD_NETWORK, threadTask);
        threadManager.StartThread(THREAD_NETWORK);
        
    } catch (const std::exception& e) {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Thread creation failed: %hs", e.what());
    }
}
```

### Lock Acquisition Failures
```cpp
void HandleLockFailure() {
    ThreadLockHelper lock(threadManager, "contested_lock", 500);
    
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to acquire lock - using fallback");
        UseFallbackMethod();
        return;
    }
    
    ProcessWithLock();
}
```

### Exception Safety in Threads
```cpp
void ExceptionSafeThread() {
    auto safeTask = []() {
        try {
            while (threadManager.GetThreadStatus(THREAD_AI_PROCESSING) == ThreadStatus::Running) {
                if (threadManager.threadVars.bIsShuttingDown.load()) break;
                
                try {
                    PerformRiskyOperation();
                } catch (const std::runtime_error& e) {
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"Operation failed: %hs", e.what());
                    continue; // Recover and try the next iteration
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"Thread fatal error: %hs", e.what());
        } catch (...) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Thread unknown fatal error");
        }
    };
    
    threadManager.SetThread(THREAD_AI_PROCESSING, safeTask);
}
```

---

## Debug Information

### Enable ThreadManager Debug Logging
```cpp
// Add to your precompiled header before ThreadManager.h is included:
#define _DEBUG_THREADMANAGER_

// Then in code: SetThread, StartThread, PauseThread, lock operations etc.
// all emit detailed log entries when _DEBUG is also defined.
// In release builds (_DEBUG not defined) all output is compiled out.
threadManager.SetThread(THREAD_RENDERER, renderTask); // Will log in debug build
```

### Thread Status Monitoring
```cpp
void MonitorAllThreads() {
    std::vector<ThreadNameID> threads = {
        THREAD_RENDERER,
        THREAD_NETWORK,
        THREAD_AI_PROCESSING,
        THREAD_LOADER,
        THREAD_FILEIO
    };
    
    for (auto threadId : threads) {
        if (threadManager.DoesThreadExist(threadId)) {
            ThreadStatus     status = threadManager.GetThreadStatus(threadId);
            std::thread::id  id     = threadManager.GetThreadID(threadId);
            
            debug.logDebugMessage(LogLevel::LOG_DEBUG,
                L"Thread %hs - Status: %d, ID available: %s",
                threadManager.getThreadName(threadId).c_str(),
                static_cast<int>(status),
                (id != std::thread::id{}) ? L"Yes" : L"No");
        }
    }
}
```

### Lock Status Debugging
```cpp
void DebugLockStatus() {
    std::vector<std::string> lockNames = {
        "render_context_lock",
        "resource_queue_lock",
        "file_access_lock"
    };
    
    for (const auto& lockName : lockNames) {
        bool isLocked = threadManager.CheckLock(lockName);
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"Lock '%hs' status: %s",
            lockName.c_str(),
            isLocked ? L"LOCKED" : L"FREE");
    }
}
```

---

## Thread Safety Guidelines

### 1. Shared Data Access
```cpp
class ThreadSafeCounter {
private:
    std::atomic<int> m_count{0};
    const std::string COUNTER_LOCK = "counter_modification_lock";
    
public:
    // Simple atomic operations don't need locks
    int GetCount() const {
        return m_count.load();
    }
    
    void Increment() {
        m_count.fetch_add(1);
    }
    
    // Complex compound operations need locks
    bool IncrementIfLessThan(int threshold) {
        ThreadLockHelper lock(threadManager, COUNTER_LOCK, 100);
        if (!lock.IsLocked()) return false;
        
        if (m_count.load() < threshold) {
            m_count.fetch_add(1);
            return true;
        }
        return false;
    }
};
```

### 2. Resource Management
```cpp
class ThreadSafeResourcePool {
private:
    std::vector<std::shared_ptr<Resource>> m_resources;
    const std::string POOL_LOCK = "resource_pool_lock";
    
public:
    std::shared_ptr<Resource> AcquireResource() {
        ThreadLockHelper lock(threadManager, POOL_LOCK, 1000);
        if (!lock.IsLocked()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire resource pool lock");
            return nullptr;
        }
        
        if (m_resources.empty()) return nullptr;
        
        auto resource = m_resources.back();
        m_resources.pop_back();
        
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Resource acquired, %zu remaining",
            m_resources.size());
        return resource;
    }
    
    void ReturnResource(std::shared_ptr<Resource> resource) {
        if (!resource) return;
        
        ThreadLockHelper lock(threadManager, POOL_LOCK, 1000);
        if (lock.IsLocked()) {
            m_resources.push_back(resource);
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Resource returned, %zu total",
                m_resources.size());
        }
    }
};
```

### 3. Event System Integration
```cpp
class ThreadSafeEventQueue {
private:
    std::queue<Event> m_events;
    const std::string EVENT_QUEUE_LOCK = "event_queue_lock";
    
public:
    void PushEvent(const Event& event) {
        ThreadLockHelper lock(threadManager, EVENT_QUEUE_LOCK, 500);
        if (lock.IsLocked()) {
            m_events.push(event);
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Event queued, %zu total events",
                m_events.size());
        }
    }
    
    bool PopEvent(Event& outEvent) {
        ThreadLockHelper lock(threadManager, EVENT_QUEUE_LOCK, 100);
        if (!lock.IsLocked() || m_events.empty()) {
            return false;
        }
        
        outEvent = m_events.front();
        m_events.pop();
        return true;
    }
    
    size_t GetEventCount() {
        ThreadLockHelper lock(threadManager, EVENT_QUEUE_LOCK, 100);
        return lock.IsLocked() ? m_events.size() : 0;
    }
};
```

---

## Common Pitfalls

### 1. Deadlock Prevention
```cpp
// BAD: Potential deadlock if another thread acquires the same locks in reverse order
void DeadlockRisk() {
    ThreadLockHelper lock1(threadManager, "lock_a", 1000);
    if (lock1.IsLocked()) {
        ThreadLockHelper lock2(threadManager, "lock_b", 1000);
        if (lock2.IsLocked()) {
            ProcessData();
        }
    }
}

// GOOD: Use MultiThreadLockHelper to acquire multiple locks safely
void DeadlockSafe() {
    MultiThreadLockHelper locks(threadManager);
    
    // All-or-nothing: if any lock fails, all previous are released
    if (locks.TryLock("lock_a") && locks.TryLock("lock_b")) {
        ProcessData();
    }
    // All locks released in LIFO order on scope exit
}
```

### 2. Lock Granularity Issues
```cpp
// BAD: One coarse-grained lock serializes everything
void CoarseGrainedLock() {
    ThreadLockHelper lock(threadManager, "everything_lock", 2000);
    if (lock.IsLocked()) {
        ReadConfiguration();
        ProcessUserInput();
        UpdateGameState();
        RenderFrame();
    }
}

// GOOD: Fine-grained locks allow maximum parallelism
void FineGrainedLocks() {
    {
        ThreadLockHelper configLock(threadManager, "config_lock", 500);
        if (configLock.IsLocked()) {
            ReadConfiguration();
        }
    }
    
    {
        ThreadLockHelper inputLock(threadManager, "input_lock", 100);
        if (inputLock.IsLocked()) {
            ProcessUserInput();
        }
    }
    
    // ... other operations with their own locks
}
```

### 3. Resource Leaks with Manual Locks
```cpp
// BAD: Manual lock can be leaked on early return or exception
void ResourceLeakRisk() {
    if (threadManager.CreateLock("temp_lock")) {
        DoSomething();
        
        if (errorCondition) {
            return; // LEAK: RemoveLock never called
        }
        
        threadManager.RemoveLock("temp_lock");
    }
}

// GOOD: RAII guarantees cleanup regardless of exit path
void ResourceSafe() {
    ThreadLockHelper lock(threadManager, "temp_lock", 1000);
    if (lock.IsLocked()) {
        DoSomething();
        
        if (errorCondition) {
            return; // Safe: destructor releases the lock
        }
    }
}
```

### 4. Thread Lifetime Issues
```cpp
// BAD: Destroying resources while thread may still be using them
void ImproperShutdown() {
    threadManager.StopThread(THREAD_RENDERER);
    DestroyRenderContext(); // CRASH RISK: thread hasn't exited yet!
}

// GOOD: Wait for thread to confirm exit before destroying resources
void ProperShutdown() {
    threadManager.threadVars.bIsShuttingDown.store(true);
    threadManager.StopThread(THREAD_RENDERER);
    
    // Poll until the thread exits (max 1 second)
    int waitCount = 0;
    while (threadManager.GetThreadStatus(THREAD_RENDERER) != ThreadStatus::Stopped &&
           waitCount < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waitCount++;
    }
    
    if (threadManager.GetThreadStatus(THREAD_RENDERER) != ThreadStatus::Stopped) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Force terminating renderer thread");
        threadManager.TerminateThread(THREAD_RENDERER);
    }
    
    // Now safe to destroy resources
    DestroyRenderContext();
}
```

### 5. Using ThreadUtils from the Wrong Thread
```cpp
// BAD: Called from the creating thread -- GetCurrentThread() points to the wrong thread
void CreateRendererBad() {
    ThreadUtils::NameCurrentThread(L"Renderer"); // Names THIS thread, not the new one!
    threadManager.SetThread(THREAD_RENDERER, renderTask);
}

// GOOD: Call ThreadUtils from inside the thread body
auto renderTask = []() {
    ThreadUtils::NameCurrentThread(L"Renderer"); // Names the renderer thread correctly
    while (/* running */) { RenderFrame(); }
};
threadManager.SetThread(THREAD_RENDERER, renderTask);
// Note: SetThread() already calls NameCurrentThread() automatically on Windows
// when using the default ThreadSchedulingConfig (useEngineDefaults = true)
```

---

## Advanced Usage

### 1. Custom Thread Pool Implementation
```cpp
class CustomThreadPool {
private:
    std::vector<ThreadNameID>         m_workerThreads;
    std::queue<std::function<void()>> m_taskQueue;
    const std::string TASK_QUEUE_LOCK = "thread_pool_tasks";
    std::atomic<bool> m_running{true};
    
public:
    void Initialize(size_t workerCount) {
        // Note: Extend ThreadNameID enum to include THREAD_WORKER_1, THREAD_WORKER_2, etc.
        
        for (size_t i = 0; i < workerCount; ++i) {
            ThreadNameID workerId = static_cast<ThreadNameID>(THREAD_FILEIO + i);
            m_workerThreads.push_back(workerId);
            
            auto workerTask = [this, i]() {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Worker thread %zu started", i);
                
                while (m_running.load() &&
                       threadManager.GetThreadStatus(m_workerThreads[i]) == ThreadStatus::Running) {
                    
                    std::function<void()> task;
                    bool hasTask = false;
                    
                    {
                        ThreadLockHelper lock(threadManager, TASK_QUEUE_LOCK, 100);
                        if (lock.IsLocked() && !m_taskQueue.empty()) {
                            task = m_taskQueue.front();
                            m_taskQueue.pop();
                            hasTask = true;
                        }
                    }
                    
                    if (hasTask) {
                        try {
                            task();
                        } catch (const std::exception& e) {
                            debug.logDebugMessage(LogLevel::LOG_ERROR,
                                L"Worker %zu task exception: %hs", i, e.what());
                        }
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Worker thread %zu finished", i);
            };
            
            threadManager.SetThread(workerId, workerTask);
            threadManager.StartThread(workerId);
        }
    }
    
    void SubmitTask(std::function<void()> task) {
        ThreadLockHelper lock(threadManager, TASK_QUEUE_LOCK, 1000);
        if (lock.IsLocked()) {
            m_taskQueue.push(task);
        }
    }
    
    void Shutdown() {
        m_running.store(false);
        for (auto workerId : m_workerThreads) {
            threadManager.StopThread(workerId);
        }
    }
    
    size_t GetQueuedTaskCount() {
        ThreadLockHelper lock(threadManager, TASK_QUEUE_LOCK, 100);
        return lock.IsLocked() ? m_taskQueue.size() : 0;
    }
};
```

### 2. Thread Communication System
```cpp
class ThreadMessenger {
public:
    struct Message {
        ThreadNameID sender;
        ThreadNameID receiver;
        std::string  type;
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point timestamp;
    };
    
private:
    std::unordered_map<ThreadNameID, std::queue<Message>> m_messageQueues;
    const std::string MESSAGE_QUEUE_LOCK = "thread_messenger_lock";
    
public:
    void SendMessage(ThreadNameID sender, ThreadNameID receiver,
                     const std::string& type, const std::vector<uint8_t>& data) {
        Message msg { sender, receiver, type, data, std::chrono::steady_clock::now() };
        
        ThreadLockHelper lock(threadManager, MESSAGE_QUEUE_LOCK, 1000);
        if (lock.IsLocked()) {
            m_messageQueues[receiver].push(msg);
        }
    }
    
    bool ReceiveMessage(ThreadNameID receiver, Message& outMessage) {
        ThreadLockHelper lock(threadManager, MESSAGE_QUEUE_LOCK, 100);
        if (!lock.IsLocked()) return false;
        
        auto it = m_messageQueues.find(receiver);
        if (it == m_messageQueues.end() || it->second.empty()) return false;
        
        outMessage = it->second.front();
        it->second.pop();
        return true;
    }
    
    size_t GetMessageCount(ThreadNameID receiver) {
        ThreadLockHelper lock(threadManager, MESSAGE_QUEUE_LOCK, 100);
        if (!lock.IsLocked()) return 0;
        
        auto it = m_messageQueues.find(receiver);
        return (it != m_messageQueues.end()) ? it->second.size() : 0;
    }
};
```

### 3. Performance Monitoring
```cpp
class ThreadPerformanceMonitor {
private:
    struct ThreadMetrics {
        std::chrono::steady_clock::time_point lastUpdate;
        std::atomic<uint64_t> operationCount{0};
        std::atomic<uint64_t> totalExecutionTime{0}; // microseconds
        std::atomic<uint64_t> lockWaitTime{0};        // microseconds
    };
    
    std::unordered_map<ThreadNameID, ThreadMetrics> m_metrics;
    const std::string METRICS_LOCK = "performance_metrics_lock";
    
public:
    void InitializeThread(ThreadNameID threadId) {
        ThreadLockHelper lock(threadManager, METRICS_LOCK, 1000);
        if (lock.IsLocked()) {
            m_metrics[threadId] = ThreadMetrics{};
            m_metrics[threadId].lastUpdate = std::chrono::steady_clock::now();
        }
    }
    
    void RecordOperation(ThreadNameID threadId, uint64_t executionTimeMicros) {
        auto it = m_metrics.find(threadId);
        if (it != m_metrics.end()) {
            it->second.operationCount.fetch_add(1);
            it->second.totalExecutionTime.fetch_add(executionTimeMicros);
            it->second.lastUpdate = std::chrono::steady_clock::now();
        }
    }
    
    void RecordLockWait(ThreadNameID threadId, uint64_t waitTimeMicros) {
        auto it = m_metrics.find(threadId);
        if (it != m_metrics.end()) {
            it->second.lockWaitTime.fetch_add(waitTimeMicros);
        }
    }
    
    void PrintPerformanceReport() {
        ThreadLockHelper lock(threadManager, METRICS_LOCK, 2000);
        if (!lock.IsLocked()) return;
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== Thread Performance Report ===");
        
        for (const auto& pair : m_metrics) {
            ThreadNameID threadId    = pair.first;
            const ThreadMetrics& m  = pair.second;
            
            uint64_t operations  = m.operationCount.load();
            uint64_t totalTime   = m.totalExecutionTime.load();
            uint64_t waitTime    = m.lockWaitTime.load();
            
            double avgExec = operations > 0 ? static_cast<double>(totalTime) / operations : 0.0;
            double avgWait = operations > 0 ? static_cast<double>(waitTime)  / operations : 0.0;
            
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Thread %hs: Ops=%llu, AvgExec=%.2fus, AvgWait=%.2fus",
                threadManager.getThreadName(threadId).c_str(),
                operations, avgExec, avgWait);
        }
    }
};
```

---

## API Reference

### ThreadManager Public Methods

#### Startup
- **`bool InitialiseThreadAffinity()`** *(Windows only)*
  - Must be called once before the first `SetThread()`.
  - Queries the system LP count, validates the engine thread layout, logs the full assignment table.
  - Returns `false` on single-core systems where ideal-processor hints are not applicable.
  - Non-Windows builds always return `false` and log a skip message.

#### Thread Management
- **`void SetThread(ThreadNameID id, std::function<void()> task, bool debugMode = false [, const ThreadSchedulingConfig& scheduling = {}])`**
  - Creates a new named thread with the specified task.
  - `debugMode`: Enable debug-mode flag for `IsDebugMode()` queries.
  - `scheduling` *(Windows only)*: Optional scheduling config; defaults to `useEngineDefaults = true` which auto-assigns LP and priority per the CPGE layout.
  - No-op if a thread with this name already exists or if shutdown is in progress.

- **`void StartThread(ThreadNameID id)`**
  - Marks the thread's status as `Running`.

- **`void PauseThread(ThreadNameID id)`**
  - Sets status to `Paused` (thread should poll status and yield).

- **`void ResumeThread(ThreadNameID id)`**
  - Sets status to `Running` and notifies the `pauseCV` condition variable.

- **`void StopThread(ThreadNameID id)`**
  - Sets status to `Stopped` (thread should exit its main loop).

- **`void TerminateThread(ThreadNameID id)`**
  - Signals `Stopped`, removes the thread from the map, and joins it (or detaches on self-join).
  - Use sparingly — prefer `StopThread()` and graceful exit.

- **`bool DoesThreadExist(ThreadNameID id)`**
  - Returns `true` if a thread with this name is registered.

- **`void Cleanup()`**
  - Sets `bShutdownRequested`, notifies all threads, joins all threads, clears all locks.
  - Called automatically in the destructor. Safe to call manually first.

#### Thread Information
- **`ThreadStatus GetThreadStatus(ThreadNameID id)`**
  - Returns: `NotStarted`, `Running`, `Paused`, `Stopped`, or `Terminated`.
  - Returns `Terminated` if global shutdown is in progress.

- **`std::thread::id GetThreadID(ThreadNameID id)`**
  - Returns the OS thread ID. Returns a default-constructed `id` if shutdown is in progress.

- **`bool IsDebugMode(ThreadNameID id)`**
  - Returns `true` if `debugMode = true` was passed to `SetThread()`.
  - In debug builds (`_DEBUG`/`DEBUG`) the `ThreadInfo::debugMode` field defaults to `true` even when not explicitly set.

- **`ThreadInfo& GetThreadInfoUnsafe(ThreadNameID id)`**
  - Returns a reference to the raw `ThreadInfo` struct.
  - **Unsafe** — call only while `threadsMutex` is already held, or when no other threads are modifying the thread map. For safe read-only queries, prefer `GetThreadStatus()`, `GetThreadID()`, and `IsDebugMode()`.

- **`std::string getThreadName(ThreadNameID id)`**
  - Converts a `ThreadNameID` to the engine's human-readable thread name string (e.g., `"GE-Rendering-Thread"`).

#### Lock Management
- **`bool CreateLock(const std::string& lockName)`**
  - Creates a new named lock owned by the calling thread. Returns `false` if it already exists.

- **`bool CheckLock(const std::string& lockName)`**
  - Returns `true` if the lock exists and is currently held.

- **`bool RemoveLock(const std::string& lockName)`**
  - Decrements `lockCount`; frees the lock and notifies waiters only when `lockCount` reaches zero.
  - Only the owning thread can call this. Returns `false` if the lock doesn't exist or the caller is not the owner.

- **`bool TryLock(const std::string& lockName, int timeoutMillisecs = 1000)`**
  - Creates the lock if it doesn't exist, or waits up to `timeoutMillisecs` for another thread to release it.
  - **Re-entrant:** if the calling thread already owns the lock, `lockCount` is incremented and `true` is returned immediately with no blocking.
  - Returns `false` if the lock is held by another thread and the timeout expires.
  - Returns `false` immediately if `IsDestroying` is set.

#### Thread Variables
- **`ThreadVariables& threadVars`**
  - Reference to the `ThreadVariables` singleton (`GetInstance()`).
  - See [Section 6.4](#64-thread-variables-and-shared-state) for the full field table.

#### Public Fields
- **`bool IsDestroying`**
  - Set to `true` before the destructor calls `Cleanup()`. `TryLock` returns `false` immediately when this is set. Can be checked in thread loops as an additional shutdown signal.

---

### ThreadSchedulingConfig Struct *(Windows only)*

Used as the optional last argument to `SetThread()`.

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `idealCore` | `DWORD` | `MAXDWORD` | Logical processor index for `SetThreadIdealProcessor()`. `MAXDWORD` = use engine default. |
| `priority` | `int` | `THREAD_PRIORITY_NORMAL` | Win32 priority constant. |
| `useEngineDefaults` | `bool` | `true` | When `true`, overrides both fields with the built-in CPGE defaults for the given `ThreadNameID`. |

---

### ThreadUtils Class *(Windows only, static methods)*

All methods operate on the **calling thread** (`GetCurrentThread()`). Must be invoked from **inside** the thread function body, not from the creating thread.

- **`static void NameCurrentThread(const wchar_t* name)`**
  - Registers a descriptive name via `SetThreadDescription()`.
  - Visible in VS 2022 debugger, PIX for Windows, RenderDoc, and WPA.
  - Requires Windows 10 1607+.

- **`static bool PreferCore(DWORD coreIndex)`**
  - Soft LP hint via `SetThreadIdealProcessor()`. OS may still migrate the thread.
  - Returns `false` if `coreIndex` is out of range for this system.
  - **Recommended for production.**

- **`static bool ForceCore(DWORD coreIndex)`**
  - Hard-pins the thread to a single LP via `SetThreadAffinityMask()`.
  - Disables Windows load balancing, Intel Thread Director, and Ryzen multi-CCD distribution.
  - Returns `false` if `coreIndex` is out of range.
  - **Debug, profiling, and benchmarking only. Never use in production.**

- **`static void SetPriority(int priority)`**
  - Sets the scheduling priority via `SetThreadPriority()`.
  - Valid constants: `THREAD_PRIORITY_IDLE`, `THREAD_PRIORITY_LOWEST`, `THREAD_PRIORITY_BELOW_NORMAL`, `THREAD_PRIORITY_NORMAL`, `THREAD_PRIORITY_ABOVE_NORMAL`, `THREAD_PRIORITY_HIGHEST`.
  - **Never use `THREAD_PRIORITY_TIME_CRITICAL` in production** — it can preempt OS audio drivers.

- **`static DWORD GetLogicalProcessorCount()`**
  - Returns the total LP count from `GetSystemInfo()`, including P-cores, E-cores, and hyper-threads.

---

### ThreadLockHelper Class

- **`ThreadLockHelper(ThreadManager& tm, const std::string& lockName, int timeoutMs = 1000, bool silent = false)`**
  - Acquires the named lock via `TryLock()` in the constructor.
  - `silent`: When `true`, no timeout warning is logged on failure.

- **`bool IsLocked() const`** — Returns `true` if the lock was successfully acquired.
- **`void Release()`** — Manually releases the lock before destructor. Safe to call multiple times.
- **`~ThreadLockHelper()`** — Releases the lock if still held.

---

### MultiThreadLockHelper Class

- **`MultiThreadLockHelper(ThreadManager& tm)`** — Initializes with a `ThreadManager` reference.
- **`bool TryLock(const std::string& lockName, int timeoutMs = 1000)`**
  - Acquires an additional lock. If this call fails, **all previously acquired locks are released**.
  - Returns `true` on success.
- **`~MultiThreadLockHelper()`** — Releases all acquired locks in LIFO order.

---

### ThreadStatus Enum
| Value | Meaning |
| --- | --- |
| `NotStarted` | Thread registered but `StartThread()` not yet called |
| `Running` | Thread is actively executing |
| `Paused` | Thread is paused; waiting for `ResumeThread()` |
| `Stopped` | Thread has exited or been signalled to stop |
| `Terminated` | Thread was force-terminated or global shutdown is active |

---

### ThreadNameID Enum

| Enum Value | Engine Thread Name |
| --- | --- |
| `THREAD_LOADER` | `GE-Loader-Thread` |
| `THREAD_RENDERER` | `GE-Rendering-Thread` |
| `THREAD_NETWORK` | `GE-Network-Thread` |
| `THREAD_AI_PROCESSING` | `GE-AI-Thread` |
| `THREAD_FILEIO` | `GE-FileIO-Processing-Thread` |

To add a new thread type, add an entry to `ThreadNameID` in ThreadManager.h **and** a matching `case` in `getThreadName()` in ThreadManager.cpp. Also add a default LP/priority entry in the `switch` inside `SetThread()` if you want it covered by `useEngineDefaults`.

---

## Conclusion

The ThreadManager and ThreadLockHelper classes provide a robust foundation for multi-threaded game engine development. The Windows scheduling layer (`ThreadSchedulingConfig`, `ThreadUtils`, `InitialiseThreadAffinity`) adds production-grade core affinity and priority management with zero overhead on other platforms.

Key takeaways:
- Call `InitialiseThreadAffinity()` before the first `SetThread()` on Windows
- Use RAII locks (`ThreadLockHelper`) — never manual lock/unlock pairs
- Check `bIsShuttingDown` and `GetThreadStatus()` regularly in thread loops
- Use `PreferCore()` (soft hint) in production, `ForceCore()` only for profiling
- `_DEBUG_THREADMANAGER_` must be defined alongside `_DEBUG` to see verbose TM log output
- Re-entrant locking is safe — the same thread can re-acquire its own lock via `TryLock`
- `GetThreadInfoUnsafe()` is the public raw accessor; prefer the safe per-field getters for general use

For additional examples and advanced usage patterns, refer to the source code and debug output from your specific implementation.
