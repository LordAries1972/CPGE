/* -------------------------------------------------------------------------------------
// ThreadManager Class Developer Guide
//
// The ThreadManager class simplifies thread management by providing safe creation,
// control, and monitoring of threads.
//
// Here's how to use it :-
//
// =========
// 1. Setup
// =========
// Include ThreadManager.h and create an instance :
//
// #include "ThreadManager.h"
//
// ThreadManager threadManager;
//
// ===================
// 2 Creating Threads
//===================
//Use SetThread() to define and start a thread :
//
//threadManager.SetThread(THREAD_RENDERER, []() {
//    while (threadManager.GetThreadStatus(THREAD_RENDERER) == ThreadStatus::Running) {
//        // Task logic here
//    }
//    });
//
// Threads are named (e.g., "THREAD_RENDERER") and stored internally.
//
// =======================
// 3. Controlling Threads
// =======================
// Start / Pause / Resume / Stop:
//
// threadManager.StartThread(THREAD_RENDERER);  // Starts if not running
// threadManager.PauseThread(THREAD_RENDERER);  // Pauses execution
// threadManager.ResumeThread(THREAD_RENDERER); // Resumes from pause
// threadManager.StopThread(THREAD_RENDERER);   // Graceful stop (sets status)
//
// Terminate: Forcefully detaches a thread :
//
// threadManager.TerminateThread(THREAD_RENDERER); // Use sparingly!
//
// =============================
// 4. Thread Status & Debugging
// =============================
// Check status :
//
// ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
//
// Debug mode(auto - enabled in debug builds) :
//
// bool isDebug = threadManager.IsDebugMode(THREAD_RENDERER);
//
// ===========
// 5) Cleanup
// ===========
// Call Cleanup() to join / detach all threads(automatically called in destructor) :
//
//    threadManager.Cleanup(); // Blocks until threads finish
//
// ==========
// Key Notes
// ==========
// Thread - Safety: All operations are mutex - protected.
//
// Graceful Shutdown : Tasks should check ThreadStatus to respond to pauses / stops.
//
// Avoid Terminate : Prefer StopThread() for safe cleanup.
//
// This class ensures structured thread management with minimal boilerplate.
// Refer to DEBUG / Visual Studio 2022 console logs for runtime feedback.
// ---------------------------------------------------------------------------------------
*/
#include "Includes.h"
#include "ThreadManager.h"
#include "Debug.h"

extern Debug debug;

//-------------------------------------------------------------------------------------------------
// ThreadManager Debug Logging Helpers
//-------------------------------------------------------------------------------------------------
// These macros enforce CPGE debug output rules for this module.
// They compile out completely unless _DEBUG_THREADMANAGER_ and _DEBUG are both defined.
#if defined(_DEBUG_THREADMANAGER_) && defined(_DEBUG)
    #define TM_LOG_LEVEL(lvl, msg)          debug.logLevelMessage((lvl), (msg))
    #define TM_LOG_DEBUG(lvl, fmt, ...)     debug.logDebugMessage((lvl), (fmt), ##__VA_ARGS__)
#else
    #define TM_LOG_LEVEL(lvl, msg)          do { (void)(lvl); (void)(msg); } while (0)
    #define TM_LOG_DEBUG(lvl, fmt, ...)     do { (void)(lvl); (void)(fmt); } while (0)
#endif


ThreadManager::ThreadManager() :
    bShutdownRequested(false),
    bHasCleanedUp(false),
    IsDestroying(false)
{
    TM_LOG_LEVEL(LogLevel::LOG_INFO, L"ThreadManager initialized.");
}

ThreadManager::~ThreadManager() {
    if (!IsDestroying) {
        IsDestroying = true;
        Cleanup();
        TM_LOG_LEVEL(LogLevel::LOG_INFO, L"ThreadManager destroyed.");
    }
}

std::string ThreadManager::getThreadName(ThreadNameID id) {
    switch (id) 
    {
        case THREAD_LOADER:             return "GE-Loader-Thread";
        case THREAD_RENDERER:           return "GE-Rendering-Thread";
        case THREAD_NETWORK:            return "GE-Network-Thread";
        case THREAD_AI_PROCESSING:      return "GE-AI-Thread";
        case THREAD_FILEIO:             return "GE-FileIO-Processing-Thread";
        default:                        return "Unknown";
    }
}

std::wstring ThreadManager::StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

//-------------------------------------------------------------------------------------------------
// ThreadUtils - Windows thread scheduling and naming utilities (PLATFORM_WINDOWS only).
//-------------------------------------------------------------------------------------------------
// All methods operate on the CALLING thread (GetCurrentThread()).
// Must be called from within the thread function body, not from the creating thread.
//-------------------------------------------------------------------------------------------------
#if defined(PLATFORM_WINDOWS)

void ThreadUtils::NameCurrentThread(const wchar_t* name) {
    // SetThreadDescription requires Windows 10 1607 (Build 14393) or later.
    // The name is visible in VS 2022 debugger, PIX for Windows, RenderDoc, and WPA.
    HRESULT hr = SetThreadDescription(GetCurrentThread(), name);

    #if defined(_DEBUG_THREADMANAGER_) && defined(_DEBUG)
        if (SUCCEEDED(hr))
            debug.logLevelMessage(LogLevel::LOG_INFO,
                std::wstring(L"[ThreadUtils] Thread named: ") + name);
        else
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                std::wstring(L"[ThreadUtils] SetThreadDescription failed for: ") + name);
    #else
        (void)hr; // Suppress unused-variable warning in release builds
    #endif
}

bool ThreadUtils::PreferCore(DWORD coreIndex) {
    // SetThreadIdealProcessor() provides a soft scheduling hint to the Windows kernel.
    // The OS retains the right to migrate the thread for load balancing, power management,
    // and Intel Thread Director (hybrid P+E core) policy decisions.
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    // Validate the requested LP index before calling the API.
    if (coreIndex >= si.dwNumberOfProcessors) {
        #if defined(_DEBUG_THREADMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[ThreadUtils] PreferCore(" + std::to_wstring(coreIndex) +
                L") out of range -- system has " + std::to_wstring(si.dwNumberOfProcessors) +
                L" logical processor(s).");
        #endif
        return false;
    }

    // Request the ideal processor. Returns the previous ideal processor, or MAXDWORD on failure.
    DWORD prev = SetThreadIdealProcessor(GetCurrentThread(), coreIndex);
    bool  ok   = (prev != MAXDWORD);

    #if defined(_DEBUG_THREADMANAGER_) && defined(_DEBUG)
        if (ok)
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"[ThreadUtils] Ideal processor -> LP " + std::to_wstring(coreIndex) +
                L" (was LP " + std::to_wstring(prev) + L").");
        else
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[ThreadUtils] SetThreadIdealProcessor failed for LP " +
                std::to_wstring(coreIndex) + L".");
    #endif

    return ok;
}

bool ThreadUtils::ForceCore(DWORD coreIndex) {
    // SetThreadAffinityMask() hard-pins the thread to a single logical processor.
    // This completely prevents Windows from migrating the thread for any reason, including:
    //   - thermal throttling compensation
    //   - Intel Thread Director P-core/E-core placement
    //   - multi-CCD Ryzen load distribution
    // USE ONLY FOR DEBUG, PROFILING, AND BENCHMARKING -- never in production game builds.
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    // Safety check before constructing the affinity mask.
    if (coreIndex >= si.dwNumberOfProcessors) {
        #if defined(_DEBUG_THREADMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[ThreadUtils] ForceCore(" + std::to_wstring(coreIndex) +
                L") out of range -- system has " + std::to_wstring(si.dwNumberOfProcessors) +
                L" logical processor(s).");
        #endif
        return false;
    }

    // Build a bitmask for the target LP only (e.g., LP 2 -> bitmask 0x4).
    DWORD_PTR mask = (DWORD_PTR)1 << coreIndex;
    bool      ok   = (SetThreadAffinityMask(GetCurrentThread(), mask) != 0);

    #if defined(_DEBUG_THREADMANAGER_) && defined(_DEBUG)
        if (ok)
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[ThreadUtils] Hard affinity set -> LP " + std::to_wstring(coreIndex) +
                L". DEBUG/PROFILING USE ONLY -- disables Windows load balancing.");
        else
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"[ThreadUtils] SetThreadAffinityMask failed for LP " +
                std::to_wstring(coreIndex) + L".");
    #endif

    return ok;
}

void ThreadUtils::SetPriority(int priority) {
    // SetThreadPriority adjusts the scheduling weight relative to other threads.
    // Avoid THREAD_PRIORITY_TIME_CRITICAL in production; it can preempt audio drivers,
    // Windows system services, and even interrupt the message pump.
    bool ok = (SetThreadPriority(GetCurrentThread(), priority) != 0);

    #if defined(_DEBUG_THREADMANAGER_) && defined(_DEBUG)
        if (ok) {
            // Map the priority integer constant to a readable label for logging.
            const wchar_t* label = L"UNKNOWN";
            switch (priority) {
                case THREAD_PRIORITY_ABOVE_NORMAL:  label = L"ABOVE_NORMAL";    break;
                case THREAD_PRIORITY_NORMAL:        label = L"NORMAL";          break;
                case THREAD_PRIORITY_BELOW_NORMAL:  label = L"BELOW_NORMAL";    break;
                case THREAD_PRIORITY_HIGHEST:       label = L"HIGHEST";         break;
                case THREAD_PRIORITY_LOWEST:        label = L"LOWEST";          break;
                case THREAD_PRIORITY_TIME_CRITICAL: label = L"TIME_CRITICAL";   break;
                case THREAD_PRIORITY_IDLE:          label = L"IDLE";            break;
            }
            debug.logLevelMessage(LogLevel::LOG_INFO,
                std::wstring(L"[ThreadUtils] Thread priority -> ") + label + L".");
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[ThreadUtils] SetThreadPriority failed.");
        }
    #else
        (void)ok; // Suppress unused-variable warning in release builds
    #endif
}

DWORD ThreadUtils::GetLogicalProcessorCount() {
    // dwNumberOfProcessors from SYSTEM_INFO includes all logical processors:
    // P-cores, E-cores, and hyper-threaded siblings on Intel hybrid CPUs.
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
}

#endif // PLATFORM_WINDOWS

//-------------------------------------------------------------------------------------------------
// ThreadManager::InitialiseThreadAffinity
//-------------------------------------------------------------------------------------------------
// Queries the system logical processor count, validates that the recommended engine thread
// layout can be satisfied, and logs the full LP assignment table at startup.
// Must be called BEFORE any SetThread() so the processor count is known and logged.
//-------------------------------------------------------------------------------------------------
bool ThreadManager::InitialiseThreadAffinity() {
#if defined(PLATFORM_WINDOWS)
    // Query hardware LP count via GetSystemInfo() -- includes all hyper-threads and E-cores.
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    m_processorCount = si.dwNumberOfProcessors;

    // Always log the LP count -- this is mandatory startup diagnostic information.
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[ThreadManager] ======== Thread Affinity Report ========");
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[ThreadManager] System logical processor count: " +
        std::to_wstring(m_processorCount));

    // A single-core system cannot benefit from ideal processor hints.
    if (m_processorCount < 2) {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"[ThreadManager] Only 1 logical processor detected -- multi-core affinity hints not applicable.");
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"[ThreadManager] =========================================");
        return false;
    }

    // Log the recommended engine thread -> logical processor mapping.
    // LP 0 is reserved for the main thread, Windows message pump, and OS scheduler work.
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[ThreadManager] Scheduling mode: SetThreadIdealProcessor() (soft hints -- recommended for production).");
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[ThreadManager] Recommended engine thread layout:");
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[ThreadManager]   LP 0  : Main thread / Window message pump / Windows OS");

    // Helper lambda to log a layout slot; warns when the LP is not available on this machine.
    auto logSlot = [&](DWORD lp, const wchar_t* desc) {
        if (lp < m_processorCount) {
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"[ThreadManager]   LP " + std::to_wstring(lp) + L"  : " + desc);
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[ThreadManager]   LP " + std::to_wstring(lp) + L"  : " + desc +
                L" (NOT AVAILABLE -- system has only " +
                std::to_wstring(m_processorCount) + L" LP(s); scheduler will decide)");
        }
    };

    logSlot(1, L"GE-AI-Thread                 [THREAD_PRIORITY_NORMAL]");
    logSlot(2, L"GE-Rendering-Thread          [THREAD_PRIORITY_ABOVE_NORMAL]");
    logSlot(3, L"GE-Loader-Thread             [THREAD_PRIORITY_NORMAL]");
    logSlot(4, L"GE-FileIO-Processing-Thread  [THREAD_PRIORITY_NORMAL]");

    // LP 5+ for audio and background jobs -- only log if enough processors exist.
    if (m_processorCount > 5) {
        logSlot(5, L"Audio / Worker jobs          [THREAD_PRIORITY_HIGHEST -- set in SoundManager]");
    }

    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[ThreadManager] Note: SetThreadAffinityMask() (ForceCore) is available for "
        L"debugging and profiling only -- do NOT use in production builds.");
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[ThreadManager] =========================================");

    return true;

#else
    // Non-Windows platforms do not expose SetThreadIdealProcessor; log and skip gracefully.
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"[ThreadManager] Platform thread affinity API not available -- skipping.");
    return false;
#endif
}

void ThreadManager::SetThread(const ThreadNameID id, std::function<void()> task, bool debugMode
#if defined(PLATFORM_WINDOWS)
    , const ThreadSchedulingConfig& scheduling
#endif
) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) {
        TM_LOG_LEVEL(LogLevel::LOG_WARNING, L"Cannot create new thread during shutdown");
        return;
    }

    std::string name = this->getThreadName(id);
    if (threads.find(name) != threads.end()) {
        TM_LOG_LEVEL(LogLevel::LOG_WARNING,
            L"Thread with name '" + StringToWString(name) + L"' already exists.");
        return;
    }

    TM_LOG_LEVEL(LogLevel::LOG_INFO,
        L"Setting up thread: " + StringToWString(name));

    ThreadInfo info = { std::thread::id(), ThreadStatus::NotStarted, debugMode };

    std::thread newThread([this, id, task, name
#if defined(PLATFORM_WINDOWS)
        , scheduling   // Captured by value so the lambda owns a copy of the config
#endif
    ]() {
        {
            std::lock_guard<std::mutex> lock(threadsMutex);
            if (!bShutdownRequested) {
                auto& info = GetThreadInfoUnsafe(id);
                info.status   = ThreadStatus::Running;
                info.threadID = std::this_thread::get_id();
            }
        }

        if (!bShutdownRequested) {
            // -----------------------------------------------------------------
            // Apply thread scheduling configuration from inside the new thread.
            // GetCurrentThread() resolves to THIS thread's handle only when
            // called from within the thread itself -- not from the creator.
            // -----------------------------------------------------------------
            #if defined(PLATFORM_WINDOWS)
            {
                // Determine effective ideal core and priority.
                // When useEngineDefaults=true the engine fills in the recommended
                // values for each known ThreadNameID; the caller's explicit values
                // (or the default {MAXDWORD, NORMAL}) are overridden automatically.
                DWORD effectiveCore     = scheduling.idealCore;
                int   effectivePriority = scheduling.priority;

                if (scheduling.useEngineDefaults) {
                    // Built-in defaults match the recommended CPGE layout:
                    //   LP 0  Main/OS   -- never assign an engine thread here
                    //   LP 1  AI        NORMAL
                    //   LP 2  Renderer  ABOVE_NORMAL (rendering must run slightly ahead)
                    //   LP 3  Loader    NORMAL
                    //   LP 4  FileIO    NORMAL
                    //   LP 5  Network   NORMAL
                    switch (id) {
                        case THREAD_AI_PROCESSING: effectiveCore = 1; effectivePriority = THREAD_PRIORITY_NORMAL;       break;
                        case THREAD_RENDERER:      effectiveCore = 2; effectivePriority = THREAD_PRIORITY_ABOVE_NORMAL; break;
                        case THREAD_LOADER:        effectiveCore = 3; effectivePriority = THREAD_PRIORITY_NORMAL;       break;
                        case THREAD_FILEIO:        effectiveCore = 4; effectivePriority = THREAD_PRIORITY_NORMAL;       break;
                        case THREAD_NETWORK:       effectiveCore = 5; effectivePriority = THREAD_PRIORITY_NORMAL;       break;
                        default:                   break; // Unknown thread type: leave defaults
                    }
                }

                // Register the thread name with the OS debugger infrastructure.
                ThreadUtils::NameCurrentThread(StringToWString(name).c_str());

                // Apply ideal processor hint when a valid LP was resolved.
                // PreferCore() internally validates against the system LP count.
                if (effectiveCore != MAXDWORD) {
                    ThreadUtils::PreferCore(effectiveCore);
                }

                // Apply the scheduling priority for this thread.
                ThreadUtils::SetPriority(effectivePriority);
            }
            #endif // PLATFORM_WINDOWS

            TM_LOG_LEVEL(LogLevel::LOG_INFO,
                L"Thread '" + StringToWString(name) + L"' started.");
            task();
        }

        {
            std::lock_guard<std::mutex> lock(threadsMutex);
            if (!bShutdownRequested) {
                auto& info = GetThreadInfoUnsafe(id);
                info.status = ThreadStatus::Stopped;
                TM_LOG_LEVEL(LogLevel::LOG_INFO,
                    L"Thread '" + StringToWString(name) + L"' finished.");
            }
        }
        });

    threads.emplace(name, std::make_pair(std::move(newThread), info));
}

bool ThreadManager::DoesThreadExist(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return false;

    std::string name = this->getThreadName(id);
    return threads.find(name) != threads.end();
}

void ThreadManager::StartThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto it = threads.find(name);
    if (it == threads.end()) {
        TM_LOG_LEVEL(LogLevel::LOG_WARNING,
            L"No Thread with name '" + StringToWString(name) + L"' found!");
        return;
    }

    auto& info = it->second.second;
    info.threadID = it->second.first.get_id();
    info.status = ThreadStatus::Running;

    TM_LOG_LEVEL(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' started.");
}

void ThreadManager::PauseThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto& info = GetThreadInfoUnsafe(id);
    if (info.status != ThreadStatus::Running) {
        TM_LOG_LEVEL(LogLevel::LOG_WARNING,
            L"Thread '" + StringToWString(name) + L"' is not Running!");
        return;
    }

    info.status = ThreadStatus::Paused;
    TM_LOG_LEVEL(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' paused.");
}

void ThreadManager::ResumeThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto& info = GetThreadInfoUnsafe(id);
    if (info.status != ThreadStatus::Paused) {
        TM_LOG_LEVEL(LogLevel::LOG_WARNING,
            L"Thread '" + StringToWString(name) + L"' is not paused.");
        return;
    }

    info.status = ThreadStatus::Running;
    TM_LOG_LEVEL(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' resumed.");
    pauseCV.notify_all();
}

void ThreadManager::StopThread(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return;

    std::string name = this->getThreadName(id);
    auto& info = GetThreadInfoUnsafe(id);
    if (info.status == ThreadStatus::Stopped || info.status == ThreadStatus::Terminated) {
        TM_LOG_LEVEL(LogLevel::LOG_WARNING,
            L"Thread '" + StringToWString(name) + L"' is already stopped or terminated.");
        return;
    }

    info.status = ThreadStatus::Stopped;
    TM_LOG_LEVEL(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' stopped.");
}

void ThreadManager::TerminateThread(const ThreadNameID id) {
    // Acquire the thread map lock so we can safely inspect and remove thread entries.
    std::unique_lock<std::mutex> lock(threadsMutex);

    // If global shutdown is already requested then do not attempt any per-thread changes.
    if (bShutdownRequested) {
        return;
    }

    // Resolve the engine thread name for the requested ID.
    std::string name = this->getThreadName(id);

    // Locate the thread record by name.
    auto it = threads.find(name);
    if (it == threads.end()) {
        TM_LOG_LEVEL(LogLevel::LOG_WARNING,
            L"No Thread with name '" + StringToWString(name) + L"' was found.");
        return;
    }

    // Mark the thread as stopping so its task loop can exit safely.
    it->second.second.status = ThreadStatus::Stopped;

    // Wake any paused threads so they can observe the stop status and exit.
    pauseCV.notify_all();

    // Move the std::thread out of the map so we can join without holding the mutex.
    std::thread threadToJoin = std::move(it->second.first);

    // Remove the map entry now so no other code can access it while we join.
    threads.erase(it);

    // Release the lock before joining to avoid deadlocks.
    lock.unlock();

    // Join the thread if possible (never detach here, detaching causes use-after-free races).
    if (threadToJoin.joinable()) {
        try {
            // If we are attempting to terminate from the same thread, detach as a last resort.
            // This prevents a self-join deadlock, but should not happen in normal engine flows.
            if (threadToJoin.get_id() == std::this_thread::get_id()) {
                threadToJoin.detach();
            }
            else {
                threadToJoin.join();
            }
        }
        catch (const std::system_error& e) {
            TM_LOG_LEVEL(LogLevel::LOG_ERROR,
                L"Error joining thread '" + StringToWString(name) + L"': " + StringToWString(e.what()));
        }
    }

    // Report termination completion for debugging.
    TM_LOG_LEVEL(LogLevel::LOG_INFO,
        L"Thread '" + StringToWString(name) + L"' terminated.");
}


void ThreadManager::Cleanup()
{
    if (bHasCleanedUp) return;

    std::unique_lock<std::mutex> lock(threadsMutex);
    bShutdownRequested = true;
    threadVars.bIsShuttingDown = true;
    lock.unlock();

    // Notify all potentially waiting threads
    pauseCV.notify_all();

    // Give threads a chance to finish gracefully
    lock.lock();
    for (auto it = threads.begin(); it != threads.end(); ) {
        auto& thread = it->second.first;
        auto& info = it->second.second;

        info.status = ThreadStatus::Stopped;

        if (thread.joinable()) {
            try {
                if (thread.get_id() != std::this_thread::get_id()) {
                    lock.unlock();
                    thread.join();
                    lock.lock();
                }
                else {
                    thread.detach();
                }
            }
            catch (const std::system_error& e) {
                TM_LOG_LEVEL(LogLevel::LOG_ERROR,
                    L"Error joining thread '" + StringToWString(it->first) +
                    L"': " + StringToWString(e.what()));
            }
        }
        it = threads.erase(it);
    }

    // Clean up any remaining locks
    {
        std::lock_guard<std::mutex> locksGuard(locksMutex);

        // Notify any waiting threads for all locks
        for (auto& cvPair : lockConditions) {
            cvPair.second.notify_all();
        }

        if (!locks.empty()) {
            TM_LOG_LEVEL(LogLevel::LOG_WARNING,
                L"Cleaning up " + std::to_wstring(locks.size()) + L" unclaimed locks.");
            locks.clear();
        }

        // Clear condition variables map
        lockConditions.clear();
    }

    TM_LOG_LEVEL(LogLevel::LOG_INFO, L"All threads and locks cleaned up.");
    bHasCleanedUp = true;
}

ThreadStatus ThreadManager::GetThreadStatus(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return ThreadStatus::Terminated;
    return GetThreadInfoUnsafe(id).status;
}

std::thread::id ThreadManager::GetThreadID(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return std::thread::id();
    return GetThreadInfoUnsafe(id).threadID;
}

bool ThreadManager::IsDebugMode(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (bShutdownRequested) return false;
    return GetThreadInfoUnsafe(id).debugMode;
}

ThreadInfo& ThreadManager::GetThreadInfo(const ThreadNameID id) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    return GetThreadInfoUnsafe(id);
}

ThreadInfo& ThreadManager::GetThreadInfoUnsafe(const ThreadNameID id) {
    static ThreadInfo emptyThreadInfo;
    if (bShutdownRequested) return emptyThreadInfo;

    try {
        std::string name = this->getThreadName(id);
        auto it = threads.find(name);
        if (it == threads.end()) {
            TM_LOG_LEVEL(LogLevel::LOG_WARNING,
                L"No Thread with name '" + StringToWString(name) + L"' was found.");
            return emptyThreadInfo;
        }
        return it->second.second;
    }
    catch (const std::exception& e) {
        TM_LOG_LEVEL(LogLevel::LOG_ERROR, L"Exception in GetThreadInfo: " + StringToWString(e.what()));
        return emptyThreadInfo;
    }
}

bool ThreadManager::CreateLock(const std::string& lockName) {
    // Lock the lock's mutex to ensure thread-safety when accessing the locks map
    std::lock_guard<std::mutex> guard(locksMutex);

    // Check if a lock with this name already exists
    if (locks.find(lockName) != locks.end()) {
        // Lock already exists, log a warning and return false
        TM_LOG_LEVEL(LogLevel::LOG_WARNING,
            L"Lock '" + StringToWString(lockName) + L"' already exists.");
        return false;
    }

    // Create a new lock by using emplace to construct it in-place
    auto result = locks.emplace(std::piecewise_construct,
        std::forward_as_tuple(lockName),                                                                    // Key
        std::forward_as_tuple());                                                                           // Value (LockInfo constructed in-place)

    // Get a reference to the newly inserted LockInfo
    LockInfo& newLock = result.first->second;

    // Set the owner thread ID and lock state
    newLock.ownerThreadID = std::this_thread::get_id();
    // Set as locked since we're creating it
    newLock.isLocked = true;

    // Initialize as first lock acquisition for this owner thread
    newLock.lockCount = 1;

    return true;
}

bool ThreadManager::CheckLock(const std::string& lockName) {
    // Lock the lock's mutex to ensure thread-safety when accessing the locks map
    std::lock_guard<std::mutex> guard(locksMutex);

    // Check if the lock exists and is currently locked
    auto it = locks.find(lockName);
    if (it != locks.end() && it->second.isLocked) {
        // If the lock exists and is locked, return true
        return true;
    }

    // Lock doesn't exist or is not locked
    return false;
}

bool ThreadManager::RemoveLock(const std::string& lockName) {
    // Lock the lock's mutex to ensure thread-safety when accessing the locks map
    std::lock_guard<std::mutex> guard(locksMutex);

    // Find the lock
    auto it = locks.find(lockName);
    if (it == locks.end()) {
        // Lock doesn't exist, log a warning and return false
        TM_LOG_LEVEL(LogLevel::LOG_WARNING, L"Cannot remove lock '" + StringToWString(lockName) + L"' as it doesn't exist.");
        return false;
    }

    // Check if the calling thread is the owner
    if (it->second.ownerThreadID != std::this_thread::get_id()) {
        // Not the owner, log an error and return false
        TM_LOG_LEVEL(LogLevel::LOG_ERROR, L"Thread is not the owner of lock '" + StringToWString(lockName) + L"'.");
        return false;
    }

    // If this lock was acquired re-entrantly by the owner thread, decrement and keep it locked.
    // This prevents self-deadlock from nested TryLock/ThreadLockHelper usage on the same lock name.
    if (it->second.lockCount > 1)
    {
        it->second.lockCount--;
        return true;
    }

    // Defensive clamp: if lockCount is 0, treat it as 1 so we can unlock and erase safely.
    if (it->second.lockCount == 0)
    {
        it->second.lockCount = 1;
    }

    // This was the final (outermost) release.
    it->second.lockCount = 0;

    // Mark the lock as unlocked before removal (important for waiting threads)
    it->second.isLocked = false;

    // Notify any waiting threads before removing the lock
    auto cvIt = lockConditions.find(lockName);
    if (cvIt != lockConditions.end()) {
        cvIt->second.notify_all();
    }

    // Remove the lock
    locks.erase(it);

    // Clean up the condition variable if it exists
    if (cvIt != lockConditions.end()) {
        lockConditions.erase(cvIt);
    }

    return true;
}

bool ThreadManager::TryLock(const std::string& lockName, int timeoutMillisecs) {
    if (IsDestroying) return false;
    std::unique_lock<std::mutex> guard(locksMutex);

    // Find the lock
    auto it = locks.find(lockName);
    if (it == locks.end()) {
        // Lock doesn't exist, create it using emplace to construct in-place
        auto result = locks.emplace(std::piecewise_construct,
            std::forward_as_tuple(lockName),  // Key
            std::forward_as_tuple());         // Value (LockInfo constructed in-place)

        // Get a reference to the newly inserted LockInfo
        LockInfo& newLock = result.first->second;

        // Set the owner thread ID and lock state
        newLock.ownerThreadID = std::this_thread::get_id();
        newLock.isLocked = true;
        // Initialize as first lock acquisition for this owner thread
        newLock.lockCount = 1;

        return true;
    }

    // Lock exists, check if it's locked
    LockInfo& lock = it->second;
    if (lock.isLocked) {
        // If the current thread already owns this lock, allow re-entrant acquisition.
        // This prevents self-deadlock when nested engine code paths lock the same name.
        if (lock.ownerThreadID == std::this_thread::get_id())
        {
            lock.lockCount++;
            return true;
        }

        // Already locked by another thread

        // If no timeout specified, return immediately
        if (timeoutMillisecs <= 0) {
            TM_LOG_LEVEL(LogLevel::LOG_DEBUG, L"TryLock failed - lock '" + StringToWString(lockName) + L"' is already locked.");
            return false;
        }

        // With timeout, wait for the lock to be released
        TM_LOG_LEVEL(LogLevel::LOG_DEBUG,
            L"TryLock waiting for lock '" + StringToWString(lockName) +
            L"' with timeout " + std::to_wstring(timeoutMillisecs) + L"ms.");

        // Create a condition variable for this specific lock if it doesn't exist
        auto& lockCV = lockConditions[lockName];

        // Wait for the lock to be released or timeout
        bool lockAcquired = lockCV.wait_for(guard,
            std::chrono::milliseconds(timeoutMillisecs),
            [&lock]() { return !lock.isLocked; });

        // If we timed out and the lock is still locked, return false
        if (!lockAcquired) {
            TM_LOG_LEVEL(LogLevel::LOG_DEBUG,
                L"TryLock timeout - lock '" + StringToWString(lockName) +
                L"' is still locked after " + std::to_wstring(timeoutMillisecs) + L"ms.");
            return false;
        }

        // Lock was released during our wait, so we can acquire it
        TM_LOG_LEVEL(LogLevel::LOG_INFO,
            L"Lock '" + StringToWString(lockName) + L"' acquired via TryLock after waiting.");
    }

    // Lock exists but is not locked (or was released during our wait), acquire it
    lock.isLocked = true;
    lock.ownerThreadID = std::this_thread::get_id();
    lock.lockCount = 1;
    return true;
}
