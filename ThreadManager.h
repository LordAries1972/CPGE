#pragma once
//-------------------------------------------------------------------------------------------------
// ThreadManager.h - Multi-threading control interface for engine-level async operations
//-------------------------------------------------------------------------------------------------
#include "Includes.h"

enum class ThreadStatus {
    NotStarted,
    Running,
    Paused,
    Stopped,
    Terminated
};

/* ---------------------------------- */
// Please Note: You add anything to the
// ThreadNameID, you will also need to
// Modify the getThreadName() in the
// ThreadManager.cpp file
/* ---------------------------------- */
enum ThreadNameID
{
    THREAD_LOADER,
    THREAD_RENDERER,
    THREAD_MOVIE_UPDATE,
};

// --------------------------------------------
// Status Flags that are used between threads.
// 
// Change this to suit your needs.
// --------------------------------------------
class ThreadVariables {
public:
    static ThreadVariables& GetInstance() {
        static ThreadVariables instance;
        return instance;
    }

    // These defined atomics are used through-out this engine, 
    // please leave these here!
    std::atomic<bool> bLoaderTaskFinished;
    std::atomic<bool> bIsRendering;
    std::atomic<bool> bIsShuttingDown;
	std::atomic<bool> bIsResizing;
    std::atomic<bool> b2DTexturesLoaded;
    std::atomic<bool> bSettingFullScreen;

    // Add your required Atomics here!
     
    
    // Delete copy constructor and assignment operator
    ThreadVariables(const ThreadVariables&) = delete;
    ThreadVariables& operator=(const ThreadVariables&) = delete;

private:
    ThreadVariables() : 
        bLoaderTaskFinished(true), 
        bIsRendering(false),
        bIsShuttingDown(false),
        bIsResizing(false),
        b2DTexturesLoaded(false),
        bSettingFullScreen(false)
    { }
};

struct ThreadInfo {
    std::thread::id threadID;
    ThreadStatus status;
    #if defined(_DEBUG) || defined(DEBUG)
        bool debugMode = true;
    #else
        bool debugMode = false;
    #endif
};

class ThreadManager {
public:
    ThreadManager();
    ~ThreadManager();

    bool IsDestroying = false;

    // Set and start a new thread
    std::string getThreadName(const ThreadNameID id);
    void SetThread(const ThreadNameID id, std::function<void()> task, bool debugMode = false);
    void StartThread(const ThreadNameID id);
    void PauseThread(const ThreadNameID id);
    void ResumeThread(const ThreadNameID id);
    void StopThread(const ThreadNameID id);
    void TerminateThread(const ThreadNameID id);
    bool DoesThreadExist(const ThreadNameID id);
    void Cleanup();

    // Lock management functions 
    bool CreateLock(const std::string& lockName);
    bool CheckLock(const std::string& lockName);
    bool RemoveLock(const std::string& lockName);
    bool TryLock(const std::string& lockName, int timeoutMillisecs = 1000);

    // Thread-safe getters
    ThreadVariables& threadVars = ThreadVariables::GetInstance();
    ThreadStatus GetThreadStatus(const ThreadNameID id);
    ThreadInfo& GetThreadInfoUnsafe(const ThreadNameID id);
    std::thread::id GetThreadID(const ThreadNameID id);
    bool IsDebugMode(const ThreadNameID id);

private:
    bool bHasCleanedUp = false;
    char buffer[256];

    // Structure to hold lock information
    struct LockInfo {
        std::mutex mutex;                                           // The actual mutex for the lock
        std::thread::id ownerThreadID;                              // Thread ID of the owner that created this lock
        bool isLocked;                                              // Current lock state
    };

    // Map to store all locks by name
    std::unordered_map<std::string, LockInfo> locks;
    // Map to store condition variables for locks
    std::unordered_map<std::string, std::condition_variable> lockConditions;
    std::mutex locksMutex;                                          // Mutex to protect access to the locks map

    std::atomic<bool> bShutdownRequested;
    std::unordered_map<std::string, std::pair<std::thread, ThreadInfo>> threads;
    std::mutex threadsMutex;
    std::condition_variable pauseCV;

    // Helper to get thread info safely
    ThreadInfo& GetThreadInfo(const ThreadNameID id);
    // String conversion calls.
    std::wstring StringToWString(const std::string& str);
};
