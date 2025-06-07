# ExceptionHandler Class - Complete Usage Guide

## Table of Contents

1.  [Overview](#overview)
2.  [Features](#features)
3.  [Platform Support](#platform-support)
4.  [Installation and Setup](#installation-and-setup)

5.  [Basic Usage](#basic-usage)
    - [5.1 Initialization](#51-initialization)
    - [5.2 Cleanup](#52-cleanup)
    - [5.3 Basic Exception Handling](#53-basic-exception-handling)

6.  [Advanced Features](#advanced-features)
    - [6.1 Function Call Tracking](#61-function-call-tracking)
    - [6.2 Manual Stack Traces](#62-manual-stack-traces)
    - [6.3 Crash Dump Generation](#63-crash-dump-generation)
    - [6.4 Custom Exception Logging](#64-custom-exception-logging)

7.  [Convenience Macros](#convenience-macros)
8.  [Thread Safety](#thread-safety)
9.  [Configuration Options](#configuration-options)

10. [Platform-Specific Considerations](#platform-specific-considerations)
    - [10.1 Windows](#101-windows)
    - [10.2 Linux/Unix](#102-linuxunix)
    - [10.3 macOS](#103-macos)
    - [10.4 Android](#104-android)

11. [Debug vs Release Builds](#debug-vs-release-builds)
12. [Performance Considerations](#performance-considerations)
13. [Troubleshooting](#troubleshooting)
14. [Best Practices](#best-practices)
15. [Example Code](#example-code)
    - [15.1 Basic Game Engine Integration](#151-basic-game-engine-integration)
    - [15.2 Multi-threaded Application](#152-multi-threaded-application)
    - [15.3 Custom Exception Types](#153-custom-exception-types)
    - [15.4 Production Deployment](#154-production-deployment)

16. [API Reference](#api-reference)
17. [Frequently Asked Questions](#frequently-asked-questions)

---

## Overview

The `ExceptionHandler` class is a comprehensive, cross-platform exception handling system designed for game engines and high-performance applications. It provides detailed crash reporting, stack trace capture, breadcrumb tracking, and automatic crash dump generation across Windows, Linux, macOS, and Android platforms.

The system is built with performance in mind, utilizing minimal overhead during normal operation while providing extensive diagnostic information when exceptions occur. It integrates seamlessly with the engine's debug system and supports both C++ exceptions and platform-specific signals/SEH.

## Features

- **Cross-Platform Support**: Works on Windows (x64), Linux, macOS, iOS, and Android
- **Comprehensive Stack Traces**: Captures detailed stack information with symbol resolution
- **Breadcrumb Tracking**: Records the last function calls before crashes
- **Automatic Crash Dumps**: Generates platform-appropriate crash dumps
- **Thread-Safe Operation**: Safe for use in multi-threaded environments
- **Symbol Resolution**: Resolves function names and line numbers (debug builds)
- **Minimal Performance Impact**: Optimized for production environments
- **Flexible Integration**: Easy to integrate with existing error handling systems

## Platform Support

| Platform | Exception Type | Stack Traces | Crash Dumps | Symbol Resolution |
|----------|----------------|--------------|-------------|-------------------|
| Windows x64 | SEH + C++ | ✅ Full | ✅ Minidump | ✅ Debug builds |
| Linux | Signals + C++ | ✅ Full | ✅ Text report | ✅ With symbols |
| macOS | Signals + C++ | ✅ Full | ✅ Text report | ✅ With symbols |
| iOS | Signals + C++ | ✅ Limited | ✅ Text report | ⚠️ Limited |
| Android | Signals + C++ | ⚠️ Basic | ✅ Text report | ⚠️ Limited |

## Installation and Setup

### Prerequisites

1. Include the necessary header files in your project
2. Ensure the debug system is initialized before the exception handler
3. Link appropriate platform libraries (Windows: dbghelp.lib for debug builds)

### Basic Setup

```cpp
#include "ExceptionHandler.h"
#include "Debug.h"

// Global instance (automatically created)
extern ExceptionHandler exceptionHandler;
extern Debug debug;
```

### Project Configuration

**For Windows (Visual Studio):**
```cpp
// In debug builds, ensure dbghelp.lib is linked
#ifdef _DEBUG
#pragma comment(lib, "dbghelp.lib")
#endif
```

**For Linux/macOS:**
```bash
# Compile with debug symbols for better stack traces
g++ -g -rdynamic your_source.cpp
```

## Basic Usage

### 5.1 Initialization

The exception handler must be initialized early in your application lifecycle, preferably in the main function or during engine startup.

```cpp
int main() {
    // Initialize debug system first
    debug.SetLogLevel(LogLevel::LOG_INFO);
    
    // Initialize exception handler
    if (!exceptionHandler.Initialize()) {
        debug.LogError("Failed to initialize exception handler");
        return -1;
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Exception handler initialized successfully");
    
    // Your application code here
    RunGameEngine();
    
    // Cleanup is automatic in destructor, but can be called manually
    exceptionHandler.Cleanup();
    return 0;
}
```

### 5.2 Cleanup

Cleanup is automatically handled by the destructor, but can be called manually for explicit control:

```cpp
void ShutdownEngine() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Shutting down engine...");
    
    // Manual cleanup (optional - destructor will handle it)
    exceptionHandler.Cleanup();
    
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Engine shutdown complete");
}
```

### 5.3 Basic Exception Handling

The exception handler automatically catches unhandled exceptions and platform-specific crashes. For manual exception logging:

```cpp
void RiskyFunction() {
    try {
        // Potentially dangerous code
        ProcessGameData();
        LoadGraphicsResources();
    }
    catch (const std::exception& ex) {
        // Log the exception with context
        exceptionHandler.LogException(ex, "RiskyFunction::ProcessGameData");
        
        // Continue with error handling
        HandleGameError();
    }
    catch (...) {
        // Handle unknown exceptions
        exceptionHandler.LogCustomException("Unknown exception caught", "RiskyFunction");
        throw; // Re-throw if needed
    }
}
```

## Advanced Features

### 6.1 Function Call Tracking

Track function calls to create a breadcrumb trail for debugging:

```cpp
void GameLoop() {
    RECORD_FUNCTION_CALL(); // Macro for debug builds
    
    // Or manually:
    exceptionHandler.RecordFunctionCall("GameLoop");
    
    UpdateInput();
    UpdateLogic();
    RenderFrame();
}

void UpdateInput() {
    RECORD_FUNCTION_CALL();
    
    // Input processing code
    ProcessKeyboardInput();
    ProcessMouseInput();
}

void RenderFrame() {
    RECORD_FUNCTION_CALL();
    
    // Rendering code that might crash
    renderer->DrawScene();
}
```

### 6.2 Manual Stack Traces

Capture stack traces for diagnostic purposes without exceptions:

```cpp
void DiagnosticFunction() {
    const int MAX_FRAMES = 32;
    StackFrameInfo frames[MAX_FRAMES];
    int frameCount = 0;
    
    if (exceptionHandler.GetCurrentStackTrace(frames, MAX_FRAMES, frameCount)) {
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Current call stack (%d frames):", frameCount);
        
        for (int i = 0; i < frameCount; ++i) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"  Frame %d: %hs [%hs]", 
                i, frames[i].functionName, frames[i].moduleName);
        }
    }
}
```

### 6.3 Crash Dump Generation

Control crash dump generation based on your needs:

```cpp
void ConfigureExceptionHandler() {
    // Enable crash dumps for production builds
    #ifdef NDEBUG
        exceptionHandler.SetCrashDumpEnabled(true);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Crash dumps enabled for production");
    #else
        // Disable in debug builds to use debugger instead
        exceptionHandler.SetCrashDumpEnabled(false);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Crash dumps disabled for debug builds");
    #endif
}
```

### 6.4 Custom Exception Logging

Create custom exception types and log them appropriately:

```cpp
class GameEngineException : public std::exception {
private:
    std::string message_;
    std::string context_;

public:
    GameEngineException(const std::string& message, const std::string& context = "")
        : message_(message), context_(context) {}
    
    const char* what() const noexcept override {
        return message_.c_str();
    }
    
    const std::string& GetContext() const { return context_; }
};

void HandleCustomException() {
    try {
        throw GameEngineException("Graphics initialization failed", "DirectX11Renderer::Initialize");
    }
    catch (const GameEngineException& ex) {
        exceptionHandler.LogException(ex, ex.GetContext().c_str());
        
        // Handle the specific exception type
        RecoverFromGraphicsError();
    }
}
```

## Convenience Macros

The system provides several macros for easier integration:

```cpp
// Function call recording (debug builds only)
#define RECORD_FUNCTION_CALL() exceptionHandler.RecordFunctionCall(__FUNCTION__)

// Exception logging with context
#define LOG_EXCEPTION(ex, context) exceptionHandler.LogException(ex, context)

// Custom exception logging
#define LOG_CUSTOM_EXCEPTION(msg, context) exceptionHandler.LogCustomException(msg, context)

// Usage examples:
void MyFunction() {
    RECORD_FUNCTION_CALL();
    
    try {
        DoSomethingRisky();
    }
    catch (const std::exception& ex) {
        LOG_EXCEPTION(ex, "MyFunction::DoSomethingRisky");
    }
}
```

## Thread Safety

The exception handler is fully thread-safe and can be used across multiple threads:

```cpp
#include "ThreadManager.h"

void WorkerThread() {
    exceptionHandler.RecordFunctionCall("WorkerThread");
    
    try {
        // Thread-specific work
        ProcessDataInBackground();
    }
    catch (const std::exception& ex) {
        // Thread-safe exception logging
        exceptionHandler.LogException(ex, "WorkerThread::ProcessDataInBackground");
    }
}

void StartBackgroundProcessing() {
    ThreadManager threadManager;
    
    threadManager.SetThread(THREAD_FILEIO, []() {
        WorkerThread();
    });
    
    threadManager.StartThread(THREAD_FILEIO);
}
```

## Configuration Options

### Crash Dump Settings

```cpp
void ConfigureCrashDumps() {
    // Enable/disable crash dump generation
    exceptionHandler.SetCrashDumpEnabled(true);
    
    // Dumps are automatically saved with timestamps:
    // Format: CrashDump_[timestamp].dmp (Windows)
    // Format: CrashDump_[timestamp].txt (Unix-like systems)
}
```

### Debug Output Control

```cpp
// Control debug output in Debug.h
#define _DEBUG_EXCEPTIONHANDLER_  // Enable exception handler debug output

// In your code:
void SetupDebugOutput() {
    debug.SetLogLevel(LogLevel::LOG_DEBUG);  // Show all debug messages
    
    // Exception handler will now output detailed information
    exceptionHandler.Initialize();
}
```

## Platform-Specific Considerations

### 10.1 Windows

**Features:**
- Full SEH (Structured Exception Handling) support
- Minidump generation with configurable detail levels
- Symbol resolution using DbgHelp API (debug builds)
- Support for both x86 and x64 architectures

**Configuration:**
```cpp
#if defined(_WIN32) || defined(_WIN64)
void ConfigureWindowsExceptions() {
    // Debug builds get full symbol information
    #ifdef _DEBUG
        // Symbols are automatically initialized
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Windows debug symbols enabled");
    #endif
    
    // Configure minidump types based on build
    exceptionHandler.SetCrashDumpEnabled(true);
}
#endif
```

### 10.2 Linux/Unix

**Features:**
- Signal-based exception handling (SIGSEGV, SIGABRT, etc.)
- Backtrace support with symbol resolution
- Text-based crash reports
- C++ exception demangling

**Configuration:**
```cpp
#if defined(__linux__)
void ConfigureLinuxExceptions() {
    // Ensure debug symbols are available
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Linux signal handlers installed");
    
    // Enable crash reporting
    exceptionHandler.SetCrashDumpEnabled(true);
}
#endif
```

### 10.3 macOS

**Features:**
- Similar to Linux with additional Mach exception support
- Full backtrace and symbol resolution
- Compatible with Xcode debugging

**Configuration:**
```cpp
#if defined(__APPLE__)
void ConfigureMacOSExceptions() {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"macOS exception handlers installed");
    exceptionHandler.SetCrashDumpEnabled(true);
}
#endif
```

### 10.4 Android

**Features:**
- Basic signal handling
- Limited stack trace capabilities
- Crash reports compatible with Android NDK

**Configuration:**
```cpp
#if defined(__ANDROID__)
void ConfigureAndroidExceptions() {
    // Android has limited symbol resolution
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Android exception handlers installed");
    exceptionHandler.SetCrashDumpEnabled(true);
}
#endif
```

## Debug vs Release Builds

### Debug Builds
- Full symbol resolution and line number information
- Function call tracking enabled
- Detailed stack traces with source file information
- Higher memory usage for debugging features

### Release Builds
- Optimized for performance
- Basic stack traces with module information
- Crash dump generation prioritized
- Minimal overhead during normal operation

```cpp
void ConfigureForBuildType() {
    #ifdef _DEBUG
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Debug build: Full exception handling enabled");
        // Keep crash dumps disabled to use debugger
        exceptionHandler.SetCrashDumpEnabled(false);
    #else
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Release build: Optimized exception handling");
        // Enable crash dumps for production
        exceptionHandler.SetCrashDumpEnabled(true);
    #endif
}
```

## Performance Considerations

### Overhead Analysis
- **Normal Operation**: < 0.1% performance impact
- **Function Call Recording**: ~5-10 CPU cycles per call (debug only)
- **Exception Handling**: Only impacts crash scenarios
- **Memory Usage**: ~50KB for buffers and state

### Optimization Tips
```cpp
void OptimizeExceptionHandling() {
    // In performance-critical sections, minimize function call recording
    #ifdef _DEBUG
        // Only record calls in debug builds
        RECORD_FUNCTION_CALL();
    #endif
    
    // Use manual exception handling for critical paths
    try {
        CriticalPerformanceCode();
    }
    catch (...) {
        // Minimal exception handling
        exceptionHandler.LogCustomException("Critical path exception", "OptimizeExceptionHandling");
        throw;
    }
}
```

## Troubleshooting

### Common Issues

**1. Initialization Failures**
```cpp
if (!exceptionHandler.Initialize()) {
    // Check debug output for specific error messages
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Exception handler initialization failed");
    
    // Common causes:
    // - Debug system not initialized first
    // - Insufficient permissions
    // - Platform-specific library issues
}
```

**2. Missing Symbols**
```cpp
#ifdef _DEBUG
void CheckSymbolResolution() {
    StackFrameInfo frames[10];
    int frameCount;
    
    if (exceptionHandler.GetCurrentStackTrace(frames, 10, frameCount)) {
        for (int i = 0; i < frameCount; ++i) {
            if (strlen(frames[i].functionName) == 0) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, 
                    L"Symbol resolution failed for frame %d", i);
            }
        }
    }
}
#endif
```

**3. Crash Dump Issues**
```cpp
void VerifyCrashDumpSettings() {
    // Ensure write permissions in current directory
    exceptionHandler.SetCrashDumpEnabled(true);
    
    // Test crash dump generation (use carefully!)
    #ifdef TESTING_CRASH_DUMPS
        exceptionHandler.LogCustomException("Test crash dump", "VerifyCrashDumpSettings");
    #endif
}
```

## Best Practices

### 1. Early Initialization
```cpp
int main() {
    // Initialize debug system first
    debug.SetLogLevel(LogLevel::LOG_INFO);
    
    // Initialize exception handler early
    if (!exceptionHandler.Initialize()) {
        return -1;
    }
    
    // Continue with application initialization
    return RunApplication();
}
```

### 2. Strategic Function Call Recording
```cpp
// Record calls in main game loop functions
void GameLoop() {
    RECORD_FUNCTION_CALL();
    UpdateGame();
    RenderGame();
}

// Skip recording in frequently called utility functions
inline float FastMath(float x) {
    // No function call recording for performance
    return x * x;
}
```

### 3. Context-Rich Exception Handling
```cpp
void LoadGameAssets() {
    try {
        LoadTextures();
        LoadModels();
        LoadSounds();
    }
    catch (const std::exception& ex) {
        // Provide specific context about what was being loaded
        std::string context = "LoadGameAssets::LoadTextures - Asset: " + currentAssetName;
        exceptionHandler.LogException(ex, context.c_str());
        
        // Attempt recovery
        UseDefaultAssets();
    }
}
```

### 4. Production vs Development Configuration
```cpp
void ConfigureExceptionHandlerForEnvironment() {
    #ifdef DEVELOPMENT_BUILD
        // Development: Prefer debugger, detailed logging
        exceptionHandler.SetCrashDumpEnabled(false);
        debug.SetLogLevel(LogLevel::LOG_DEBUG);
    #elif defined(TESTING_BUILD)
        // Testing: Enable crash dumps, moderate logging
        exceptionHandler.SetCrashDumpEnabled(true);
        debug.SetLogLevel(LogLevel::LOG_INFO);
    #else
        // Production: Crash dumps only, minimal logging
        exceptionHandler.SetCrashDumpEnabled(true);
        debug.SetLogLevel(LogLevel::LOG_ERROR);
    #endif
}
```

## Example Code

### 15.1 Basic Game Engine Integration

```cpp
#include "ExceptionHandler.h"
#include "Debug.h"
#include "Renderer.h"
#include "SceneManager.h"

class GameEngine {
private:
    std::shared_ptr<Renderer> renderer;
    std::unique_ptr<SceneManager> sceneManager;

public:
    bool Initialize() {
        RECORD_FUNCTION_CALL();
        
        try {
            // Initialize exception handling first
            if (!exceptionHandler.Initialize()) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to initialize exception handler");
                return false;
            }
            
            // Initialize renderer
            renderer = std::make_shared<DX11Renderer>();
            if (!renderer->Initialize(GetMainWindow(), GetInstance())) {
                throw std::runtime_error("Renderer initialization failed");
            }
            
            // Initialize scene manager
            sceneManager = std::make_unique<SceneManager>();
            if (!sceneManager->Initialize(renderer)) {
                throw std::runtime_error("Scene manager initialization failed");
            }
            
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Game engine initialized successfully");
            return true;
        }
        catch (const std::exception& ex) {
            exceptionHandler.LogException(ex, "GameEngine::Initialize");
            return false;
        }
    }
    
    void Run() {
        RECORD_FUNCTION_CALL();
        
        try {
            while (IsRunning()) {
                UpdateGame();
                RenderGame();
            }
        }
        catch (const std::exception& ex) {
            exceptionHandler.LogException(ex, "GameEngine::Run - Main loop");
            Shutdown();
        }
    }
    
    void UpdateGame() {
        RECORD_FUNCTION_CALL();
        
        try {
            // Update game logic
            sceneManager->UpdateScene(GetDeltaTime());
        }
        catch (const std::exception& ex) {
            exceptionHandler.LogException(ex, "GameEngine::UpdateGame");
            // Continue running but log the error
        }
    }
    
    void RenderGame() {
        RECORD_FUNCTION_CALL();
        
        try {
            renderer->RenderFrame();
        }
        catch (const std::exception& ex) {
            exceptionHandler.LogException(ex, "GameEngine::RenderGame");
            // Continue running but log the error
        }
    }
    
    ~GameEngine() {
        RECORD_FUNCTION_CALL();
        Shutdown();
    }
    
private:
    void Shutdown() {
        RECORD_FUNCTION_CALL();
        
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Shutting down game engine");
        
        // Cleanup in reverse order
        sceneManager.reset();
        renderer.reset();
        
        // Exception handler cleanup is automatic
    }
};

int main() {
    // Initialize debug system
    debug.SetLogLevel(LogLevel::LOG_INFO);
    
    // Create and run game engine
    GameEngine engine;
    if (engine.Initialize()) {
        engine.Run();
    }
    
    return 0;
}
```

### 15.2 Multi-threaded Application

```cpp
#include "ThreadManager.h"
#include "ExceptionHandler.h"

class MultiThreadedEngine {
private:
    ThreadManager threadManager;
    std::atomic<bool> running{true};

public:
    bool Initialize() {
        if (!exceptionHandler.Initialize()) {
            return false;
        }
        
        // Start rendering thread
        threadManager.SetThread(THREAD_RENDERER, [this]() {
            RenderThread();
        });
        
        // Start asset loading thread
        threadManager.SetThread(THREAD_LOADER, [this]() {
            LoaderThread();
        });
        
        // Start network thread
        threadManager.SetThread(THREAD_NETWORK, [this]() {
            NetworkThread();
        });
        
        threadManager.StartThread(THREAD_RENDERER);
        threadManager.StartThread(THREAD_LOADER);
        threadManager.StartThread(THREAD_NETWORK);
        
        return true;
    }
    
private:
    void RenderThread() {
        exceptionHandler.RecordFunctionCall("RenderThread");
        
        while (running && threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running) {
            try {
                // Rendering work
                renderer->RenderFrame();
                std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
            }
            catch (const std::exception& ex) {
                exceptionHandler.LogException(ex, "RenderThread::RenderFrame");
                // Continue rendering loop
            }
        }
    }
    
    void LoaderThread() {
        exceptionHandler.RecordFunctionCall("LoaderThread");
        
        while (running && threadManager.GetThreadStatus(THREAD_LOADER) == ThreadStatus::Running) {
            try {
                // Asset loading work
                ProcessAssetQueue();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            catch (const std::exception& ex) {
                exceptionHandler.LogException(ex, "LoaderThread::ProcessAssetQueue");
                // Continue loading loop
            }
        }
    }
    
    void NetworkThread() {
        exceptionHandler.RecordFunctionCall("NetworkThread");
        
        while (running && threadManager.GetThreadStatus(THREAD_NETWORK) == ThreadStatus::Running) {
            try {
                // Network processing
                ProcessNetworkMessages();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            catch (const std::exception& ex) {
                exceptionHandler.LogException(ex, "NetworkThread::ProcessNetworkMessages");
                // Continue network loop
            }
        }
    }
};
```

### 15.3 Custom Exception Types

```cpp
#include "ExceptionHandler.h"

// Custom exception hierarchy for game engine
class GameEngineException : public std::exception {
protected:
    std::string message_;
    std::string context_;
    std::string component_;

public:
    GameEngineException(const std::string& message, 
                       const std::string& context = "", 
                       const std::string& component = "")
        : message_(message), context_(context), component_(component) {}
    
    const char* what() const noexcept override { return message_.c_str(); }
    const std::string& GetContext() const { return context_; }
    const std::string& GetComponent() const { return component_; }
    
    virtual std::string GetFullDescription() const {
        std::string desc = component_.empty() ? "" : "[" + component_ + "] ";
        desc += message_;
        if (!context_.empty()) {
            desc += " (Context: " + context_ + ")";
        }
        return desc;
    }
};

class RendererException : public GameEngineException {
public:
    RendererException(const std::string& message, const std::string& context = "")
        : GameEngineException(message, context, "Renderer") {}
};

class AssetException : public GameEngineException {
public:
    AssetException(const std::string& message, const std::string& context = "")
        : GameEngineException(message, context, "AssetLoader") {}
};

class NetworkException : public GameEngineException {
public:
    NetworkException(const std::string& message, const std::string& context = "")
        : GameEngineException(message, context, "Network") {}
};

// Exception handling functions
void HandleRendererError() {
    try {
        // Renderer operations that might fail
        InitializeGraphicsDevice();
        CreateRenderTargets();
    }
    catch (const RendererException& ex) {
        exceptionHandler.LogException(ex, ex.GetContext().c_str());
        
        // Specific renderer error recovery
        FallbackToSoftwareRenderer();
    }
    catch (const std::exception& ex) {
        exceptionHandler.LogException(ex, "HandleRendererError::Unknown");
        throw; // Re-throw unknown exceptions
    }
}

void HandleAssetLoading() {
    try {
        LoadCriticalAssets();
    }
    catch (const AssetException& ex) {
        exceptionHandler.LogException(ex, ex.GetContext().c_str());
        
        // Asset loading error recovery
        LoadDefaultAssets();
    }
}

// Global exception handler for uncaught exceptions
void SetupGlobalExceptionHandler() {
    std::set_terminate([]() {
        try {
            auto ex = std::current_exception();
            if (ex) {
                std::rethrow_exception(ex);
            }
        }
        catch (const GameEngineException& ex) {
            exceptionHandler.LogCustomException(
                ex.GetFullDescription().c_str(), 
                "Global::UncaughtGameEngineException"
            );
        }
        catch (const std::exception& ex) {
            exceptionHandler.LogException(ex, "Global::UncaughtStdException");
        }
        catch (...) {
            exceptionHandler.LogCustomException(
                "Unknown uncaught exception", 
                "Global::UncaughtUnknownException"
            );
        }
        
        std::abort();
    });
}
```

### 15.4 Production Deployment

```cpp
#include "ExceptionHandler.h"
#include "Configuration.h"

class ProductionExceptionManager {
private:
    std::string crashReportDirectory;
    bool telemetryEnabled;
    
public:
    bool Initialize(const std::string& crashDir, bool enableTelemetry) {
        crashReportDirectory = crashDir;
        telemetryEnabled = enableTelemetry;
        
        // Create crash report directory if it doesn't exist
        CreateDirectoryIfNeeded(crashReportDirectory);
        
        // Initialize exception handler with production settings
        if (!exceptionHandler.Initialize()) {
            return false;
        }
        
        // Enable crash dumps for production
        exceptionHandler.SetCrashDumpEnabled(true);
        
        // Set up custom exception callback
        SetupProductionExceptionCallback();
        
        return true;
    }
    
private:
    void SetupProductionExceptionCallback() {
        // Install a custom terminate handler for production
        std::set_terminate([this]() {
            HandleProductionCrash();
            std::abort();
        });
    }
    
    void HandleProductionCrash() {
        try {
            // Log basic crash information
            exceptionHandler.LogCustomException(
                "Production crash occurred", 
                "ProductionExceptionManager::HandleProductionCrash"
            );
            
            // Collect additional crash context
            CollectCrashContext();
            
            // Send telemetry if enabled
            if (telemetryEnabled) {
                SendCrashTelemetry();
            }
            
            // Save user data if possible
            AttemptUserDataSave();
            
        }
        catch (...) {
            // Ensure we don't crash in the crash handler
        }
    }
    
    void CollectCrashContext() {
        try {
            // Get current stack trace
            const int MAX_FRAMES = 32;
            StackFrameInfo frames[MAX_FRAMES];
            int frameCount = 0;
            
            if (exceptionHandler.GetCurrentStackTrace(frames, MAX_FRAMES, frameCount)) {
                debug.logDebugMessage(LogLevel::LOG_CRITICAL, 
                    L"Production crash stack trace (%d frames):", frameCount);
                
                for (int i = 0; i < std::min(frameCount, 10); ++i) {
                    debug.logDebugMessage(LogLevel::LOG_CRITICAL, 
                        L"  Frame %d: %hs [%hs]", 
                        i, frames[i].functionName, frames[i].moduleName);
                }
            }
            
            // Log system information
            LogSystemInformation();
            
        }
        catch (...) {
            // Ignore errors during crash context collection
        }
    }
    
    void SendCrashTelemetry() {
        try {
            // Send anonymized crash data to telemetry service
            // (Implementation depends on your telemetry system)
            
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"Crash telemetry sent successfully");
        }
        catch (...) {
            // Ignore telemetry errors during crash
        }
    }
    
    void AttemptUserDataSave() {
        try {
            // Attempt to save critical user data before crash
            SaveGameProgress();
            SaveUserSettings();
            
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                L"User data saved successfully during crash");
        }
        catch (...) {
            // Ignore save errors during crash
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                L"Failed to save user data during crash");
        }
    }
    
    void LogSystemInformation() {
        try {
            // Log relevant system information for crash analysis
            debug.logLevelMessage(LogLevel::LOG_INFO, L"=== System Information ===");
            
            // Add system-specific information logging here
            // This would integrate with your system utilities
            
        }
        catch (...) {
            // Ignore errors during system info logging
        }
    }
    
    void CreateDirectoryIfNeeded(const std::string& path) {
        // Platform-specific directory creation
        #if defined(_WIN32) || defined(_WIN64)
            CreateDirectoryA(path.c_str(), nullptr);
        #else
            mkdir(path.c_str(), 0755);
        #endif
    }
    
    void SaveGameProgress() {
        // Save critical game state
        // Implementation depends on your save system
    }
    
    void SaveUserSettings() {
        // Save user configuration
        // Implementation depends on your settings system
    }
};

// Usage in main application
int main() {
    ProductionExceptionManager exceptionManager;
    
    if (!exceptionManager.Initialize("./crash_reports/", true)) {
        return -1;
    }
    
    // Run the application
    return RunGameApplication();
}
```

## API Reference

### Core Methods

#### `bool Initialize()`
Initializes the exception handling system and installs platform-specific handlers.

**Returns:** `true` if initialization successful, `false` otherwise.

**Example:**
```cpp
if (!exceptionHandler.Initialize()) {
    debug.LogError("Exception handler initialization failed");
    return false;
}
```

#### `void Cleanup()`
Cleans up all resources and restores previous exception handlers. Called automatically in destructor.

**Example:**
```cpp
// Manual cleanup (optional)
exceptionHandler.Cleanup();
```

#### `void LogException(const std::exception& ex, const char* context = nullptr)`
Logs a C++ exception with full stack trace and context information.

**Parameters:**
- `ex`: The exception to log
- `context`: Optional context string describing where the exception occurred

**Example:**
```cpp
try {
    RiskyOperation();
}
catch (const std::exception& ex) {
    exceptionHandler.LogException(ex, "MyClass::RiskyOperation");
}
```

#### `void LogCustomException(const char* message, const char* context = nullptr)`
Logs a custom exception message with stack trace.

**Parameters:**
- `message`: Custom error message
- `context`: Optional context string

**Example:**
```cpp
if (criticalResourceMissing) {
    exceptionHandler.LogCustomException("Critical resource not found", "ResourceLoader::Initialize");
}
```

#### `void RecordFunctionCall(const char* functionName)`
Records a function call in the breadcrumb trail for debugging.

**Parameters:**
- `functionName`: Name of the function being called

**Example:**
```cpp
void MyFunction() {
    exceptionHandler.RecordFunctionCall("MyFunction");
    // or use the macro:
    RECORD_FUNCTION_CALL();
}
```

#### `bool GetCurrentStackTrace(StackFrameInfo* frames, int maxFrames, int& frameCount)`
Captures the current stack trace without requiring an exception.

**Parameters:**
- `frames`: Array to store stack frame information
- `maxFrames`: Maximum number of frames to capture
- `frameCount`: Output parameter for actual number of frames captured

**Returns:** `true` if stack trace captured successfully.

**Example:**
```cpp
StackFrameInfo frames[32];
int frameCount;
if (exceptionHandler.GetCurrentStackTrace(frames, 32, frameCount)) {
    // Process stack trace
}
```

#### `void SetCrashDumpEnabled(bool enabled)`
Enables or disables automatic crash dump generation.

**Parameters:**
- `enabled`: `true` to enable crash dumps, `false` to disable

**Example:**
```cpp
#ifdef PRODUCTION_BUILD
    exceptionHandler.SetCrashDumpEnabled(true);
#else
    exceptionHandler.SetCrashDumpEnabled(false);
#endif
```

#### `static ExceptionHandler& GetInstance()`
Gets the singleton instance of the exception handler.

**Returns:** Reference to the global exception handler instance.

**Example:**
```cpp
ExceptionHandler& handler = ExceptionHandler::GetInstance();
handler.LogCustomException("Manual exception", "SomeContext");
```

### Data Structures

#### `struct StackFrameInfo`
Contains detailed information about a single stack frame.

**Members:**
- `uint64_t address`: Memory address of the frame
- `char functionName[MAX_SYMBOL_NAME_LENGTH]`: Function name if available
- `char moduleName[MAX_MODULE_NAME_LENGTH]`: Module/library name
- `uint32_t lineNumber`: Source line number if available
- `char fileName[512]`: Source file name if available
- `uint64_t displacement`: Offset from symbol start

#### `struct ExceptionDetails`
Comprehensive exception information structure.

**Members:**
- `uint32_t exceptionCode`: Platform-specific exception code
- `uint64_t exceptionAddress`: Address where exception occurred
- `uint32_t threadId`: Thread ID where exception happened
- `char exceptionDescription[512]`: Human-readable description
- `int frameCount`: Number of valid frames captured
- `uint64_t timeStamp`: When the exception occurred
- `uint32_t processId`: Process ID for context
- `std::unique_ptr<StackFrameInfo[]> stackFrames`: Complete stack trace

### Constants

```cpp
const int MAX_STACK_FRAMES = 64;           // Maximum stack frames to capture
const int MAX_SYMBOL_NAME_LENGTH = 1024;   // Maximum symbol name length
const int MAX_MODULE_NAME_LENGTH = 256;    // Maximum module name length
const int LAST_CALLS_BUFFER_SIZE = 5;      // Number of function calls to track
```

### Macros

```cpp
#define RECORD_FUNCTION_CALL()                          // Records current function (debug only)
#define LOG_EXCEPTION(ex, context)                      // Logs exception with context
#define LOG_CUSTOM_EXCEPTION(msg, context)              // Logs custom exception
```

## Frequently Asked Questions

### Q: Does the exception handler impact performance during normal operation?
**A:** The performance impact is minimal (< 0.1%) during normal operation. Function call recording in debug builds adds only 5-10 CPU cycles per call, and exception handling only activates during actual crashes.

### Q: Can I use this in a multi-threaded application?
**A:** Yes, the exception handler is fully thread-safe. All public methods use appropriate synchronization, and it can safely handle exceptions from multiple threads simultaneously.

### Q: What happens if the exception handler itself crashes?
**A:** The system is designed to be robust against recursive failures. Critical sections are protected, and the handler attempts to log what it can before terminating. In worst-case scenarios, the system will fall back to default platform behavior.

### Q: Can I customize the crash dump format?
**A:** Currently, the system uses platform-appropriate formats (Windows minidumps, Unix text reports). The crash dump generation can be enabled/disabled, but the format is not directly customizable.

### Q: How do I integrate this with my existing error handling system?
**A:** The exception handler is designed to complement existing systems. You can continue using your current exception handling and add the exception handler for comprehensive crash reporting. Use the `LogException` and `LogCustomException` methods to bridge your existing system.

### Q: Does this work with third-party libraries?
**A:** Yes, the system will catch unhandled exceptions from third-party code. However, symbol resolution quality depends on whether debug symbols are available for those libraries.

### Q: Can I disable function call tracking in release builds?
**A:** Yes, the `RECORD_FUNCTION_CALL()` macro automatically becomes a no-op in release builds. You can also manually control this by defining or undefining `_DEBUG`.

### Q: How large are the crash dump files?
**A:** This varies by platform and configuration:
- Windows debug builds: 10-100MB (full memory dump)
- Windows release builds: 1-10MB (normal minidump)
- Unix-like systems: 1-5KB (text-based crash report)

### Q: Can I send crash reports automatically to a server?
**A:** The exception handler focuses on crash detection and logging. Automatic reporting would need to be implemented in your application using the logged information and crash dumps.

### Q: What information is captured in stack traces?
**A:** Stack traces include:
- Memory addresses for all frames
- Function names (when symbols available)
- Module/library names
- Source file names and line numbers (debug builds)
- Offset from function start
- Last few function calls (breadcrumb trail)

### Q: Is this compatible with debuggers?
**A:** Yes, the system is designed to work alongside debuggers. In debug builds, you might want to disable crash dump generation to prioritize debugger attachment.

### Q: Can I use this on mobile platforms?
**A:** Basic support is included for iOS and Android, though with platform limitations:
- iOS: Limited symbol resolution due to App Store restrictions
- Android: Basic signal handling with NDK compatibility

### Q: How do I handle exceptions in callback functions?
**A:** Wrap callback implementations with try-catch blocks and use the exception handler:

```cpp
void MyCallback(void* userData) {
    try {
        // Callback implementation
        ProcessCallbackData(userData);
    }
    catch (const std::exception& ex) {
        exceptionHandler.LogException(ex, "MyCallback");
    }
}
```

### Q: Can I get stack traces without exceptions?
**A:** Yes, use the `GetCurrentStackTrace` method for diagnostic stack traces during normal execution:

```cpp
void DiagnosticFunction() {
    StackFrameInfo frames[16];
    int frameCount;
    if (exceptionHandler.GetCurrentStackTrace(frames, 16, frameCount)) {
        // Process diagnostic stack trace
    }
}
```

---

## Conclusion

The ExceptionHandler class provides a comprehensive, cross-platform solution for exception handling and crash reporting in game engines and high-performance applications. Its design prioritizes minimal performance impact during normal operation while providing extensive diagnostic information when problems occur.

Key benefits include:
- **Robust crash detection** across all supported platforms
- **Detailed diagnostic information** including stack traces and breadcrumb trails
- **Thread-safe operation** suitable for multi-threaded applications
- **Flexible integration** with existing error handling systems
- **Production-ready features** including crash dump generation

By following the examples and best practices in this guide, you can effectively integrate comprehensive exception handling into your application, improving debugging capabilities and providing valuable crash information for production deployments.

For additional support or questions not covered in this guide, refer to the source code documentation and platform-specific debugging resources.