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

7. [Lock Management](#lock-management)
   - [7.1 Creating and Managing Locks](#71-creating-and-managing-locks)
   - [7.2 Using ThreadLockHelper (RAII)](#72-using-threadlockhelper-raii)
   - [7.3 Multiple Lock Management](#73-multiple-lock-management)

8. [Complete Examples](#complete-examples)
   - [8.1 Basic Thread Example](#81-basic-thread-example)
   - [8.2 Renderer Thread Example](#82-renderer-thread-example)
   - [8.3 Thread Synchronization Example](#83-thread-synchronization-example)
   - [8.4 Producer-Consumer Pattern](#84-producer-consumer-pattern)

9.  [Best Practices](#best-practices)
10. [Error Handling](#error-handling)
11. [Debug Information](#debug-information)
12. [Thread Safety Guidelines](#thread-safety-guidelines)
13. [Common Pitfalls](#common-pitfalls)
14. [Advanced Usage](#advanced-usage)
15. [API Reference](#api-reference)

---

## Introduction

The ThreadManager class provides a comprehensive thread management system for game engines and multi-threaded applications. It simplifies thread creation, control, and synchronization while providing robust lock management capabilities through the ThreadLockHelper RAII wrapper.

This guide covers both basic and advanced usage patterns, complete with working examples and best practices for production environments.

---

## ThreadManager Class Overview

The ThreadManager class offers:

- **Thread Creation & Management**: Named threads with lifecycle control
- **Thread Status Monitoring**: Real-time status tracking and debugging
- **Lock Management**: Thread-safe lock creation and synchronization
- **Graceful Shutdown**: Safe cleanup and resource management
- **Shared Variables**: Thread-safe shared state management

### Key Features:

- Named thread identification using `ThreadNameID` enum
- Automatic thread lifecycle management
- RAII-style lock management via ThreadLockHelper
- Debug logging integration
- Thread-safe operations with mutex protection

---

## ThreadLockHelper Overview

ThreadLockHelper provides RAII (Resource Acquisition Is Initialization) style lock management:

- **Automatic Lock Acquisition**: Locks acquired in constructor
- **Automatic Release**: Locks released in destructor
- **Timeout Support**: Configurable lock acquisition timeouts
- **Multiple Lock Support**: Manage multiple locks safely
- **Exception Safety**: Locks automatically released on exceptions

---

## Prerequisites

### Required Headers:
```cpp
#include "ThreadManager.h"
#include "ThreadLockHelper.h"
#include "Debug.h"
```

### Global Instances:
```cpp
extern ThreadManager threadManager;
extern Debug debug;
```

### Thread Name Registration:
Ensure your thread names are registered in `ThreadNameID` enum and corresponding `getThreadName()` function in ThreadManager.cpp.

---

## Basic Setup

### 1. Include Required Headers
```cpp
#include "ThreadManager.h"
#include "ThreadLockHelper.h"
```

### 2. Access Global ThreadManager Instance
```cpp
// ThreadManager is typically declared as a global singleton
extern ThreadManager threadManager;
```

### 3. Initialize in Your Application
```cpp
// ThreadManager initializes automatically upon construction
// No explicit initialization required
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
    
    // Create and start the thread
    threadManager.SetThread(THREAD_RENDERER, renderTask, true); // true = debug mode
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
    ThreadStatus networkStatus = threadManager.GetThreadStatus(THREAD_NETWORK);
    
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
    
    // Get complete thread info
    ThreadInfo& info = threadManager.GetThreadInfo(THREAD_RENDERER);
    
    debug.logDebugMessage(LogLevel::LOG_DEBUG, 
        L"Renderer thread - Status: %d, Debug: %s", 
        static_cast<int>(info.status),
        isDebugMode ? L"enabled" : L"disabled");
}
```

### 6.4 Thread Variables and Shared State

#### Using Atomic Variables
```cpp
void ManageSharedState() {
    // Access shared thread variables
    auto& vars = threadManager.threadVars;
    
    // Set rendering state
    vars.bIsRendering.store(true);
    
    // Check if loader is finished
    if (vars.bLoaderTaskFinished.load()) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Loader task completed");
        // Continue with main game loop
    }
    
    // Handle resizing
    if (vars.bIsResizing.load()) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Window resize in progress");
        // Pause rendering temporarily
        vars.bIsRendering.store(false);
    }
}
```

---

## Lock Management

### 7.1 Creating and Managing Locks

#### Manual Lock Management
```cpp
void ManualLockExample() {
    const std::string lockName = "resource_access_lock";
    
    // Create a lock
    if (threadManager.CreateLock(lockName)) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Lock created successfully");
        
        // Perform thread-safe operations
        AccessSharedResource();
        
        // Remove the lock when done
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

### 7.2 Using ThreadLockHelper (RAII)

#### Basic RAII Lock Usage
```cpp
void RAIILockExample() {
    // Lock is automatically acquired in constructor
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
    // Create lock without timeout warnings (silent = true)
    ThreadLockHelper lock(threadManager, "background_task_lock", 500, true);
    
    if (lock.IsLocked()) {
        // Perform background operations
        ProcessBackgroundTask();
    }
    // No warning logged if lock fails
}
```

#### Manual Lock Release
```cpp
void ManualReleaseExample() {
    ThreadLockHelper lock(threadManager, "file_access_lock", 2000);
    
    if (lock.IsLocked()) {
        // Perform first operation
        WriteToFile();
        
        // Manually release lock early if needed
        lock.Release();
        
        // Lock is now released, other threads can access
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Lock released early");
    }
    
    // Destructor won't try to release again
}
```

### 7.3 Multiple Lock Management

#### Sequential Multiple Locks
```cpp
void MultiLockExample() {
    MultiThreadLockHelper locks(threadManager);
    
    // Try to acquire multiple locks in sequence
    if (!locks.TryLock("database_lock") || 
        !locks.TryLock("file_system_lock") || 
        !locks.TryLock("network_lock")) {
        
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to acquire all required locks");
        return; // All locks automatically released
    }
    
    // All locks acquired successfully
    debug.logLevelMessage(LogLevel::LOG_INFO, L"All locks acquired, performing complex operation");
    
    // Perform operations requiring multiple resources
    ComplexOperation();
    
    // All locks automatically released when 'locks' goes out of scope
}
```

---

## Complete Examples

### 8.1 Basic Thread Example

```cpp
class BasicThreadExample {
public:
    void StartExample() {
        // Create a simple worker thread
        auto workerTask = []() {
            int workCount = 0;
            
            while (threadManager.GetThreadStatus(THREAD_FILEIO) == ThreadStatus::Running) {
                // Check for shutdown
                if (threadManager.threadVars.bIsShuttingDown.load()) {
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Worker thread shutting down");
                    break;
                }
                
                // Do some work
                PerformWork(workCount++);
                
                // Sleep to prevent busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        };
        
        // Create and start the thread
        threadManager.SetThread(THREAD_FILEIO, workerTask, true);
        threadManager.StartThread(THREAD_FILEIO);
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Basic worker thread started");
    }
    
private:
    static void PerformWork(int workId) {
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Performing work item %d", workId);
        
        // Simulate work
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};
```

### 8.2 Renderer Thread Example

```cpp
class RendererThreadExample {
private:
    std::atomic<bool> m_renderingEnabled{true};
    std::atomic<int> m_frameCount{0};
    
public:
    void StartRenderer() {
        auto renderTask = [this]() {
            auto lastFrameTime = std::chrono::high_resolution_clock::now();
            
            while (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running) {
                // Handle pause/resume
                if (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Paused) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                
                // Check shutdown
                if (threadManager.threadVars.bIsShuttingDown.load()) {
                    break;
                }
                
                // Skip rendering during resize
                if (threadManager.threadVars.bIsResizing.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                
                // Render frame with lock protection
                RenderFrameThreadSafe();
                
                // Frame rate control
                auto currentTime = std::chrono::high_resolution_clock::now();
                auto frameDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastFrameTime);
                
                if (frameDuration < std::chrono::milliseconds(16)) { // 60 FPS cap
                    std::this_thread::sleep_for(std::chrono::milliseconds(16) - frameDuration);
                }
                
                lastFrameTime = std::chrono::high_resolution_clock::now();
                m_frameCount++;
            }
            
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Renderer thread completed %d frames", 
                                 m_frameCount.load());
        };
        
        threadManager.SetThread(THREAD_RENDERER, renderTask, true);
        threadManager.StartThread(THREAD_RENDERER);
    }
    
private:
    void RenderFrameThreadSafe() {
        // Use RAII lock for render context access
        ThreadLockHelper renderLock(threadManager, "render_context_lock", 100);
        
        if (!renderLock.IsLocked()) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Skipping frame - render lock unavailable");
            return;
        }
        
        // Set rendering flag
        threadManager.threadVars.bIsRendering.store(true);
        
        try {
            // Perform actual rendering
            ExecuteRenderCommands();
            SwapBuffers();
        }
        catch (const std::exception& e) {
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"Render error: %hs", e.what());
        }
        
        // Clear rendering flag
        threadManager.threadVars.bIsRendering.store(false);
        
        // Lock automatically released here
    }
    
    void ExecuteRenderCommands() {
        // Simulate rendering work
        std::this_thread::sleep_for(std::chrono::microseconds(8000)); // ~8ms render time
    }
    
    void SwapBuffers() {
        // Simulate buffer swap
        std::this_thread::sleep_for(std::chrono::microseconds(1000)); // ~1ms swap time
    }
};
```

### 8.3 Thread Synchronization Example

```cpp
class ThreadSynchronizationExample {
private:
    std::queue<int> m_dataQueue;
    const std::string QUEUE_LOCK = "data_queue_lock";
    
public:
    void StartProducerConsumer() {
        // Producer thread
        auto producerTask = [this]() {
            int dataValue = 0;
            
            while (threadManager.GetThreadStatus(THREAD_AI_PROCESSING) == ThreadStatus::Running) {
                if (threadManager.threadVars.bIsShuttingDown.load()) break;
                
                // Produce data with thread safety
                ProduceData(dataValue++);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        };
        
        // Consumer thread
        auto consumerTask = [this]() {
            while (threadManager.GetThreadStatus(THREAD_FILEIO) == ThreadStatus::Running) {
                if (threadManager.threadVars.bIsShuttingDown.load()) break;
                
                // Consume data with thread safety
                ConsumeData();
                
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            }
        };
        
        // Start both threads
        threadManager.SetThread(THREAD_AI_PROCESSING, producerTask, true);
        threadManager.SetThread(THREAD_FILEIO, consumerTask, true);
        
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
            
            // Release lock early to allow producer to continue
            lock.Release();
            
            // Process the data (outside of lock)
            ProcessData(value);
        }
    }
    
    void ProcessData(int value) {
        // Simulate data processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Processed data: %d", value);
    }
};
```

### 8.4 Producer-Consumer Pattern

```cpp
class ResourceManager {
private:
    std::vector<std::string> m_pendingLoads;
    std::vector<std::string> m_loadedResources;
    const std::string LOAD_QUEUE_LOCK = "resource_load_queue";
    const std::string LOADED_LIST_LOCK = "loaded_resources_list";
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
        
        // Resource loader thread
        auto loaderTask = [this]() {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Resource loader thread started");
            
            while (threadManager.GetThreadStatus(THREAD_LOADER) == ThreadStatus::Running) {
                if (threadManager.threadVars.bIsShuttingDown.load()) break;
                
                std::string resourceToLoad;
                bool hasWork = false;
                
                // Get next resource to load
                {
                    ThreadLockHelper lock(threadManager, LOAD_QUEUE_LOCK, 100);
                    if (lock.IsLocked() && !m_pendingLoads.empty()) {
                        resourceToLoad = m_pendingLoads.back();
                        m_pendingLoads.pop_back();
                        hasWork = true;
                    }
                }
                
                if (hasWork) {
                    // Load resource (outside of lock)
                    if (LoadResource(resourceToLoad)) {
                        // Add to loaded list
                        ThreadLockHelper loadedLock(threadManager, LOADED_LIST_LOCK, 1000);
                        if (loadedLock.IsLocked()) {
                            m_loadedResources.push_back(resourceToLoad);
                        }
                    }
                } else {
                    // No work available, check if we're done
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
                    
                    // Brief sleep to prevent busy waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        };
        
        // Start the loader thread
        threadManager.SetThread(THREAD_LOADER, loaderTask, true);
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
        
        // Simulate loading time
        std::this_thread::sleep_for(std::chrono::milliseconds(100 + (rand() % 200)));
        
        // Simulate occasional load failures
        bool success = (rand() % 10) != 0; // 90% success rate
        
        if (success) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"Successfully loaded: %hs", resourceName.c_str());
        } else {
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"Failed to load: %hs", resourceName.c_str());
        }
        
        return success;
    }
};
```

---

## Best Practices

### 1. Always Check Thread Status
```cpp
// Good: Check status before operations
if (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running) {
    // Perform thread operations
}

// Bad: Assume thread is running
// PerformThreadOperations(); // May fail if thread is paused/stopped
```

### 2. Use RAII Locks Whenever Possible
```cpp
// Good: RAII lock management
{
    ThreadLockHelper lock(threadManager, "resource_lock", 1000);
    if (lock.IsLocked()) {
        AccessSharedResource();
    }
    // Lock automatically released
}

// Avoid: Manual lock management (error-prone)
threadManager.CreateLock("resource_lock");
AccessSharedResource(); // What if exception occurs?
threadManager.RemoveLock("resource_lock"); // May not be reached
```

### 3. Handle Shutdown Gracefully
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
    DoWork(); // Thread may never exit
}
```

### 4. Use Appropriate Lock Timeouts
```cpp
// Good: Reasonable timeout for critical operations
ThreadLockHelper lock(threadManager, "critical_lock", 2000); // 2 seconds

// Good: Short timeout for optional operations
ThreadLockHelper lock(threadManager, "optional_lock", 100); // 100ms

// Bad: No timeout (potential deadlock)
ThreadLockHelper lock(threadManager, "risky_lock", 0);
```

### 5. Minimize Lock Scope
```cpp
// Good: Narrow lock scope
void ProcessData() {
    PrepareData(); // Outside lock
    
    {
        ThreadLockHelper lock(threadManager, "data_lock", 1000);
        if (lock.IsLocked()) {
            ModifySharedData(); // Only critical section locked
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

---

## Error Handling

### Thread Creation Errors
```cpp
void SafeThreadCreation() {
    try {
        auto threadTask = []() {
            // Thread implementation
        };
        
        // Check if thread already exists
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
        
        // Implement fallback behavior
        UseFallbackMethod();
        return;
    }
    
    // Normal processing with lock acquired
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
                    // Continue with next iteration
                    continue;
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

### Enable Debug Logging
```cpp
void EnableThreadDebugging() {
    // Debug mode is set when creating threads
    threadManager.SetThread(THREAD_RENDERER, renderTask, true); // true = debug mode
    
    // Check if debug mode is enabled
    if (threadManager.IsDebugMode(THREAD_RENDERER)) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Debug mode enabled for renderer thread");
    }
}
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
            ThreadStatus status = threadManager.GetThreadStatus(threadId);
            std::thread::id id = threadManager.GetThreadID(threadId);
            
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
    
    // Complex operations need locks
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
        
        if (m_resources.empty()) {
            return nullptr; // No resources available
        }
        
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
// BAD: Potential deadlock with nested locks
void DeadlockRisk() {
    ThreadLockHelper lock1(threadManager, "lock_a", 1000);
    if (lock1.IsLocked()) {
        ThreadLockHelper lock2(threadManager, "lock_b", 1000);
        if (lock2.IsLocked()) {
            // Another thread might acquire locks in reverse order
            ProcessData();
        }
    }
}

// GOOD: Use MultiThreadLockHelper for multiple locks
void DeadlockSafe() {
    MultiThreadLockHelper locks(threadManager);
    
    // Locks acquired in consistent order
    if (locks.TryLock("lock_a") && locks.TryLock("lock_b")) {
        ProcessData();
    }
    // All locks automatically released in reverse order
}
```

### 2. Lock Granularity Issues
```cpp
// BAD: Lock too coarse-grained
void CoarseGrainedLock() {
    ThreadLockHelper lock(threadManager, "everything_lock", 2000);
    if (lock.IsLocked()) {
        ReadConfiguration();   // Could be separate lock
        ProcessUserInput();    // Could be separate lock  
        UpdateGameState();     // Could be separate lock
        RenderFrame();         // Could be separate lock
    }
}

// GOOD: Fine-grained locks
void FineGrainedLocks() {
    // Separate operations with separate locks
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
    
    // Continue with other operations...
}
```

### 3. Resource Leaks
```cpp
// BAD: Manual resource management
void ResourceLeakRisk() {
    if (threadManager.CreateLock("temp_lock")) {
        DoSomething();
        
        if (errorCondition) {
            return; // LEAK: Lock not removed!
        }
        
        threadManager.RemoveLock("temp_lock");
    }
}

// GOOD: RAII resource management
void ResourceSafe() {
    ThreadLockHelper lock(threadManager, "temp_lock", 1000);
    if (lock.IsLocked()) {
        DoSomething();
        
        if (errorCondition) {
            return; // Safe: Lock automatically released
        }
        
        // Lock automatically released at scope end
    }
}
```

### 4. Thread Lifetime Issues
```cpp
// BAD: Not waiting for thread completion
void ImproperShutdown() {
    threadManager.StopThread(THREAD_RENDERER);
    // Immediately destroy objects that thread might be using
    DestroyRenderContext(); // CRASH RISK!
}

// GOOD: Proper shutdown sequence
void ProperShutdown() {
    // Signal shutdown
    threadManager.threadVars.bIsShuttingDown.store(true);
    
    // Stop threads
    threadManager.StopThread(THREAD_RENDERER);
    
    // Wait for graceful shutdown
    int waitCount = 0;
    while (threadManager.GetThreadStatus(THREAD_RENDERER) != ThreadStatus::Stopped && 
           waitCount < 100) { // 1 second max wait
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waitCount++;
    }
    
    // Force cleanup if needed
    if (threadManager.GetThreadStatus(THREAD_RENDERER) != ThreadStatus::Stopped) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Force terminating renderer thread");
        threadManager.TerminateThread(THREAD_RENDERER);
    }
    
    // Now safe to destroy resources
    DestroyRenderContext();
}
```

---

## Advanced Usage

### 1. Custom Thread Pool Implementation
```cpp
class CustomThreadPool {
private:
    std::vector<ThreadNameID> m_workerThreads;
    std::queue<std::function<void()>> m_taskQueue;
    const std::string TASK_QUEUE_LOCK = "thread_pool_tasks";
    std::atomic<bool> m_running{true};
    
public:
    void Initialize(size_t workerCount) {
        // Note: This example assumes you have extended ThreadNameID enum
        // to include THREAD_WORKER_1, THREAD_WORKER_2, etc.
        
        for (size_t i = 0; i < workerCount; ++i) {
            ThreadNameID workerId = static_cast<ThreadNameID>(THREAD_FILEIO + i);
            m_workerThreads.push_back(workerId);
            
            auto workerTask = [this, i]() {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Worker thread %zu started", i);
                
                while (m_running.load() && 
                       threadManager.GetThreadStatus(m_workerThreads[i]) == ThreadStatus::Running) {
                    
                    std::function<void()> task;
                    bool hasTask = false;
                    
                    // Get task from queue
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
                        // No work available, brief sleep
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
                
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Worker thread %zu finished", i);
            };
            
            threadManager.SetThread(workerId, workerTask, true);
            threadManager.StartThread(workerId);
        }
    }
    
    void SubmitTask(std::function<void()> task) {
        ThreadLockHelper lock(threadManager, TASK_QUEUE_LOCK, 1000);
        if (lock.IsLocked()) {
            m_taskQueue.push(task);
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Task submitted, %zu tasks queued", 
                                 m_taskQueue.size());
        }
    }
    
    void Shutdown() {
        m_running.store(false);
        
        // Stop all worker threads
        for (auto workerId : m_workerThreads) {
            threadManager.StopThread(workerId);
        }
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Thread pool shutdown complete");
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
        std::string type;
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point timestamp;
    };
    
private:
    std::unordered_map<ThreadNameID, std::queue<Message>> m_messageQueues;
    const std::string MESSAGE_QUEUE_LOCK = "thread_messenger_lock";
    
public:
    void SendMessage(ThreadNameID sender, ThreadNameID receiver, 
                    const std::string& type, const std::vector<uint8_t>& data) {
        Message msg;
        msg.sender = sender;
        msg.receiver = receiver;
        msg.type = type;
        msg.data = data;
        msg.timestamp = std::chrono::steady_clock::now();
        
        ThreadLockHelper lock(threadManager, MESSAGE_QUEUE_LOCK, 1000);
        if (lock.IsLocked()) {
            m_messageQueues[receiver].push(msg);
            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                L"Message sent from %hs to %hs, type: %hs",
                threadManager.getThreadName(sender).c_str(),
                threadManager.getThreadName(receiver).c_str(),
                type.c_str());
        }
    }
    
    bool ReceiveMessage(ThreadNameID receiver, Message& outMessage) {
        ThreadLockHelper lock(threadManager, MESSAGE_QUEUE_LOCK, 100);
        if (!lock.IsLocked()) return false;
        
        auto it = m_messageQueues.find(receiver);
        if (it == m_messageQueues.end() || it->second.empty()) {
            return false;
        }
        
        outMessage = it->second.front();
        it->second.pop();
        
        debug.logDebugMessage(LogLevel::LOG_DEBUG, 
            L"Message received by %hs from %hs, type: %hs",
            threadManager.getThreadName(receiver).c_str(),
            threadManager.getThreadName(outMessage.sender).c_str(),
            outMessage.type.c_str());
        
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
        std::atomic<uint64_t> lockWaitTime{0};       // microseconds
    };
    
    std::unordered_map<ThreadNameID, ThreadMetrics> m_metrics;
    const std::string METRICS_LOCK = "performance_metrics_lock";
    
public:
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
            ThreadNameID threadId = pair.first;
            const ThreadMetrics& metrics = pair.second;
            
            uint64_t operations = metrics.operationCount.load();
            uint64_t totalTime = metrics.totalExecutionTime.load();
            uint64_t waitTime = metrics.lockWaitTime.load();
            
            double avgExecutionTime = operations > 0 ? 
                static_cast<double>(totalTime) / operations : 0.0;
            double avgWaitTime = operations > 0 ? 
                static_cast<double>(waitTime) / operations : 0.0;
            
            debug.logDebugMessage(LogLevel::LOG_INFO,
                L"Thread %hs: Ops=%llu, AvgExec=%.2fμs, AvgWait=%.2fμs",
                threadManager.getThreadName(threadId).c_str(),
                operations, avgExecutionTime, avgWaitTime);
        }
    }
    
    void InitializeThread(ThreadNameID threadId) {
        ThreadLockHelper lock(threadManager, METRICS_LOCK, 1000);
        if (lock.IsLocked()) {
            m_metrics[threadId] = ThreadMetrics{};
            m_metrics[threadId].lastUpdate = std::chrono::steady_clock::now();
        }
    }
};

// Usage example with performance monitoring
class MonitoredWorker {
private:
    ThreadPerformanceMonitor& m_monitor;
    ThreadNameID m_threadId;
    
public:
    MonitoredWorker(ThreadPerformanceMonitor& monitor, ThreadNameID threadId) 
        : m_monitor(monitor), m_threadId(threadId) {
        m_monitor.InitializeThread(threadId);
    }
    
    void DoWork() {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Perform work with lock monitoring
        auto lockStart = std::chrono::high_resolution_clock::now();
        ThreadLockHelper lock(threadManager, "work_lock", 1000);
        auto lockEnd = std::chrono::high_resolution_clock::now();
        
        if (lock.IsLocked()) {
            // Record lock wait time
            auto lockWaitMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                lockEnd - lockStart).count();
            m_monitor.RecordLockWait(m_threadId, lockWaitMicros);
            
            // Do actual work
            PerformActualWork();
        }
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto executionMicros = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count();
        
        m_monitor.RecordOperation(m_threadId, executionMicros);
    }
    
private:
    void PerformActualWork() {
        // Simulate work
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
};
```

---

## API Reference

### ThreadManager Public Methods

#### Thread Management
- **`void SetThread(ThreadNameID id, std::function<void()> task, bool debugMode = false)`**
  - Creates a new thread with the specified task
  - `id`: Unique thread identifier from ThreadNameID enum
  - `task`: Function/lambda to execute in the thread
  - `debugMode`: Enable debug logging for this thread

- **`void StartThread(ThreadNameID id)`**
  - Starts a previously created thread
  - Sets thread status to Running

- **`void PauseThread(ThreadNameID id)`**
  - Pauses a running thread
  - Thread should check status and handle pause appropriately

- **`void ResumeThread(ThreadNameID id)`**
  - Resumes a paused thread
  - Sets status back to Running and notifies waiting threads

- **`void StopThread(ThreadNameID id)`**
  - Gracefully stops a thread
  - Sets status to Stopped - thread should exit its main loop

- **`void TerminateThread(ThreadNameID id)`**
  - Forcefully terminates a thread (use sparingly)
  - Detaches thread and removes from management

- **`bool DoesThreadExist(ThreadNameID id)`**
  - Returns true if thread exists in the manager

- **`void Cleanup()`**
  - Stops and joins all threads
  - Clears all locks and resources
  - Called automatically in destructor

#### Thread Information
- **`ThreadStatus GetThreadStatus(ThreadNameID id)`**
  - Returns current status: NotStarted, Running, Paused, Stopped, Terminated

- **`std::thread::id GetThreadID(ThreadNameID id)`**
  - Returns the system thread ID

- **`bool IsDebugMode(ThreadNameID id)`**
  - Returns true if debug mode is enabled for the thread

- **`std::string getThreadName(ThreadNameID id)`**
  - Converts ThreadNameID enum to human-readable string

#### Lock Management
- **`bool CreateLock(const std::string& lockName)`**
  - Creates a new named lock
  - Returns false if lock already exists

- **`bool CheckLock(const std::string& lockName)`**
  - Returns true if lock exists and is currently locked

- **`bool RemoveLock(const std::string& lockName)`**
  - Removes a lock (only owner thread can remove)
  - Notifies any waiting threads

- **`bool TryLock(const std::string& lockName, int timeoutMillisecs = 1000)`**
  - Attempts to acquire a lock with timeout
  - Creates lock if it doesn't exist
  - Returns true if lock acquired successfully

#### Thread Variables
- **`ThreadVariables& threadVars`**
  - Reference to shared atomic variables
  - Contains common flags like `bIsShuttingDown`, `bIsRendering`, etc.

### ThreadLockHelper Class

#### Constructors
- **`ThreadLockHelper(ThreadManager& tm, const std::string& lockName, int timeoutMs = 1000, bool silent = false)`**
  - Automatically acquires lock in constructor
  - `tm`: Reference to ThreadManager instance
  - `lockName`: Name of lock to acquire
  - `timeoutMs`: Maximum time to wait for lock
  - `silent`: If true, don't log timeout warnings

#### Methods
- **`bool IsLocked() const`**
  - Returns true if lock was successfully acquired

- **`void Release()`**
  - Manually releases the lock before destructor
  - Safe to call multiple times

#### Destructor
- **`~ThreadLockHelper()`**
  - Automatically releases lock if still held
  - Ensures RAII cleanup

### MultiThreadLockHelper Class

#### Constructor
- **`MultiThreadLockHelper(ThreadManager& tm)`**
  - Initializes multi-lock helper with ThreadManager reference

#### Methods
- **`bool TryLock(const std::string& lockName, int timeoutMs = 1000)`**
  - Attempts to acquire an additional lock
  - If any lock fails, all previously acquired locks are released
  - Returns true if lock acquired successfully

#### Destructor
- **`~MultiThreadLockHelper()`**
  - Automatically releases all acquired locks in reverse order (LIFO)

### ThreadStatus Enum Values
- **`NotStarted`**: Thread created but not yet started
- **`Running`**: Thread is actively executing
- **`Paused`**: Thread is paused, waiting for resume
- **`Stopped`**: Thread has been stopped gracefully
- **`Terminated`**: Thread was forcefully terminated

### ThreadNameID Enum (Extend as needed)
- **`THREAD_LOADER`**: Resource loading thread
- **`THREAD_RENDERER`**: Main rendering thread  
- **`THREAD_NETWORK`**: Network communication thread
- **`THREAD_AI_PROCESSING`**: AI/game logic processing
- **`THREAD_FILEIO`**: File I/O operations thread

---

## Conclusion

The ThreadManager and ThreadLockHelper classes provide a robust foundation for multi-threaded game engine development. By following the patterns and best practices outlined in this guide, you can create efficient, safe, and maintainable multi-threaded applications.

Key takeaways:
- Always use RAII locks (ThreadLockHelper) when possible
- Check thread status regularly and handle shutdown gracefully  
- Use appropriate lock timeouts and granularity
- Monitor thread performance and debug information
- Follow consistent patterns for thread communication
- Handle exceptions properly in threaded code

For additional examples and advanced usage patterns, refer to the source code and debug output from your specific implementation.