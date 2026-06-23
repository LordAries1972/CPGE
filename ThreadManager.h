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
    THREAD_NETWORK,
    THREAD_AI_PROCESSING,
    THREAD_FILEIO,
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
    std::atomic<bool> bLoaderTaskFinished;                          // If False, we are currently loading in resources.
    std::atomic<bool> bIsRendering;                                 // If True, We are Currently Rendering
    std::atomic<bool> bIsShuttingDown;                              // If True, Application Shutdown in progress!
	std::atomic<bool> bIsResizing;                                  // If True, we are currently resizing and reinitiating new window dimensions & resources.
    std::atomic<bool> bHasGameReset;
    std::atomic<bool> bHasReset;
    std::atomic<bool> b2DTexturesLoaded;                            // If True, DirectX 2D Textures have been loaded into memory.
    std::atomic<bool> bSettingFullScreen;
    std::atomic<bool> bInitiateFader;                               // Used for Loading Screen.

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
        bHasGameReset(false),
        b2DTexturesLoaded(false),
        bInitiateFader(false),
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

// -----------------------------------------------------------------------------------------
// ThreadSchedulingConfig - Optional scheduling hints for SetThread() on Windows platforms.
//
// idealCore        : Logical processor index passed to SetThreadIdealProcessor().
//                    MAXDWORD = use the engine's built-in default for the thread type.
// priority         : Win32 thread priority constant.
//                    Rendering = THREAD_PRIORITY_ABOVE_NORMAL
//                    Loader, FileIO, AI, Network = THREAD_PRIORITY_NORMAL
//                    Audio = THREAD_PRIORITY_HIGHEST (set inside SoundManager separately)
//                    Never use THREAD_PRIORITY_TIME_CRITICAL in production builds.
// useEngineDefaults: When true (default), the engine fills in the recommended core/priority
//                    for each known ThreadNameID. Set false to use your own explicit values.
//
// Recommended engine thread -> logical processor layout (LP 0 reserved for OS/main thread):
//   LP 1  GE-AI-Thread                [THREAD_PRIORITY_NORMAL]
//   LP 2  GE-Rendering-Thread         [THREAD_PRIORITY_ABOVE_NORMAL]
//   LP 3  GE-Loader-Thread            [THREAD_PRIORITY_NORMAL]
//   LP 4  GE-FileIO-Processing-Thread [THREAD_PRIORITY_NORMAL]
//   LP 5  Audio / Worker jobs         [THREAD_PRIORITY_HIGHEST]
// -----------------------------------------------------------------------------------------
#if defined(PLATFORM_WINDOWS)
struct ThreadSchedulingConfig {
    DWORD idealCore       = MAXDWORD;               // MAXDWORD = use engine default for the thread type
    int   priority        = THREAD_PRIORITY_NORMAL; // Scheduling weight; see layout above
    bool  useEngineDefaults = true;                 // When true, overrides idealCore/priority with built-in per-thread-type defaults
};

// -----------------------------------------------------------------------------------------
// ThreadUtils - Static utility class for Windows-only thread scheduling and naming.
//
// ALL methods operate on the CALLING thread (GetCurrentThread()).
// Call these from inside the thread function body, NEVER from the creating thread.
//
// NameCurrentThread     : Registers a name visible in VS 2022, PIX, RenderDoc, WPA.
// PreferCore            : Soft hint via SetThreadIdealProcessor() -- recommended for production.
// ForceCore             : Hard lock via SetThreadAffinityMask() -- DEBUG / PROFILING ONLY.
// SetPriority           : Adjusts CPU scheduling weight; avoid TIME_CRITICAL in production.
// GetLogicalProcessorCount : Returns LP count from GetSystemInfo().
// -----------------------------------------------------------------------------------------
class ThreadUtils {
public:
    // Registers a descriptive name for the calling thread.
    // Name is visible in Visual Studio 2022 debugger, PIX, RenderDoc, and Windows Performance Analyzer.
    static void NameCurrentThread(const wchar_t* name);

    // Hints to the Windows scheduler to prefer a given logical processor (soft hint).
    // Windows may still migrate the thread for load balancing, thermal, or hybrid-core policies.
    // Returns false if coreIndex exceeds the available logical processor count.
    static bool PreferCore(DWORD coreIndex);

    // Hard-locks the calling thread to a single logical processor via SetThreadAffinityMask().
    // Disables Windows load balancing and Intel Thread Director on hybrid CPUs.
    // USE FOR DEBUGGING, PROFILING, AND BENCHMARKING ONLY -- never enable in production builds.
    static bool ForceCore(DWORD coreIndex);

    // Sets the scheduling priority of the calling thread.
    // Do NOT use THREAD_PRIORITY_TIME_CRITICAL in production; it can starve OS drivers and audio.
    static void SetPriority(int priority);

    // Returns the number of logical processors from GetSystemInfo().
    // On hybrid CPUs (Intel P+E), this includes all P-cores, E-cores, and hyper-threads.
    static DWORD GetLogicalProcessorCount();
};
#endif // PLATFORM_WINDOWS

class ThreadManager {
public:
    ThreadManager();
    ~ThreadManager();

    bool IsDestroying = false;

    // Set and start a new thread
    std::string getThreadName(const ThreadNameID id);
    void SetThread(const ThreadNameID id, std::function<void()> task, bool debugMode = false
#if defined(PLATFORM_WINDOWS)
        // Optional scheduling hints; when useEngineDefaults=true the engine fills in
        // the recommended LP and priority for each known ThreadNameID automatically.
        , const ThreadSchedulingConfig& scheduling = {}
#endif
    );
    void StartThread(const ThreadNameID id);
    void PauseThread(const ThreadNameID id);
    void ResumeThread(const ThreadNameID id);
    void StopThread(const ThreadNameID id);
    void TerminateThread(const ThreadNameID id);
    bool DoesThreadExist(const ThreadNameID id);
    void Cleanup();

    // Queries and logs the system logical processor count and recommended engine thread layout.
    // Must be called BEFORE the first SetThread() so the core count is available.
    // Returns false if fewer than 2 logical processors are detected.
    bool InitialiseThreadAffinity();

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
    #if defined(PLATFORM_WINDOWS)
        DWORD m_processorCount = 0;             // Logical processor count; set by InitialiseThreadAffinity()
    #endif

    // Structure to hold lock information
    struct LockInfo {
        std::mutex mutex;                                           // The actual mutex for the lock
        std::thread::id ownerThreadID;                              // Thread ID of the owner that created this lock
        bool isLocked;                                              // Current lock state
        uint32_t lockCount;                                         // Re-entrant lock depth for the owning thread
    };

    // Map to store all locks by name
    std::unordered_map<std::string, LockInfo> locks;
    // Map to store condition variables for locks
    std::map<std::string, std::condition_variable> lockConditions;
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
